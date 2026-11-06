#include "core/GroundEstimate.h"

#include <algorithm>
#include <numbers>
#include <optional>

namespace SkiingInSkyrim::Core
{
    namespace
    {
        constexpr float kRadians = std::numbers::pi_v<float> / 180.0F;

        std::optional<Vec3> Normalized(Vec3 v) noexcept
        {
            const float length = Length(v);
            if (!std::isfinite(length) || length < 0.5F) {
                return std::nullopt;
            }
            const Vec3 n{ v.x / length, v.y / length, v.z / length };
            if (n.z <= 0.0F) {
                return std::nullopt;
            }
            return n;
        }

        bool IsWall(Vec3 unitNormal, float wallSlopeDegrees) noexcept
        {
            return unitNormal.z < std::cos(wallSlopeDegrees * kRadians);
        }
    }

    void GroundTracker::Reset() noexcept
    {
        estimate_ = {};
        hasPosition_ = false;
    }

    const GroundEstimate& GroundTracker::Update(const GroundSample& sample) noexcept
    {
        const float dt = std::isfinite(sample.deltaSeconds) ? std::max(sample.deltaSeconds, 0.0F) : 0.0F;

        float moved = 0.0F;
        if (hasPosition_) {
            const Vec3 delta{ sample.position.x - lastPosition_.x, sample.position.y - lastPosition_.y, 0.0F };
            moved = HorizontalLength(delta);
            if (!std::isfinite(moved) || moved > settings_.teleportDistanceUnits) {
                Reset();
                moved = 0.0F;
            }
        }
        lastPosition_ = sample.position;
        hasPosition_ = true;

        // Pick the ground normal: support normal first (it ignores side contacts), then the
        // closest-contact normal, skipping zero vectors and anything steep enough to be a wall.
        const auto support = Normalized(sample.supportNormal);
        const auto surface = Normalized(sample.surfaceNormal);
        estimate_.wallContact = (surface && IsWall(*surface, settings_.wallSlopeDegrees)) ||
                                (support && IsWall(*support, settings_.wallSlopeDegrees));

        std::optional<Vec3> ground;
        estimate_.source = NormalSource::None;
        if (sample.supported) {
            if (support && !IsWall(*support, settings_.wallSlopeDegrees)) {
                ground = support;
                estimate_.source = NormalSource::Support;
            } else if (surface && !IsWall(*surface, settings_.wallSlopeDegrees)) {
                ground = surface;
                estimate_.source = NormalSource::Surface;
            }
        }

        if (ground) {
            if (!estimate_.hasNormal) {
                estimate_.normal = *ground;
                estimate_.hasNormal = true;
            } else {
                const float distance = moved + settings_.stationarySmoothingUnitsPerSecond * dt;
                const float blend = 1.0F - std::exp(-distance / std::max(settings_.smoothingDistanceUnits, 1.0F));
                const Vec3  mixed{
                    estimate_.normal.x + (ground->x - estimate_.normal.x) * blend,
                    estimate_.normal.y + (ground->y - estimate_.normal.y) * blend,
                    estimate_.normal.z + (ground->z - estimate_.normal.z) * blend,
                };
                if (const auto unit = Normalized(mixed)) {
                    estimate_.normal = *unit;
                }
            }
        }

        if (sample.supported) {
            estimate_.unsupportedSeconds = 0.0F;
            estimate_.contact = Contact::Grounded;
        } else {
            estimate_.unsupportedSeconds += dt;
            // Going straight down the smoothed slope at the current speed explains this much descent.
            const float horizontalSpeed = HorizontalLength(sample.velocity);
            const float slopeTangent = estimate_.normal.z > 0.05F ?
                                           HorizontalLength(estimate_.normal) / estimate_.normal.z :
                                           20.0F;
            const float expectedDescent = horizontalSpeed * slopeTangent;
            const bool  falling = sample.velocity.z < -(expectedDescent + settings_.fallMarginMetersPerSecond);
            if (falling || estimate_.unsupportedSeconds > settings_.airGraceSeconds) {
                estimate_.contact = Contact::Airborne;
            }
        }
        return estimate_;
    }
}
