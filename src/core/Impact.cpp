#include "core/Impact.h"

#include <algorithm>

namespace SkiingInSkyrim::Core
{
    namespace
    {
        Vec3 Sub(Vec3 a, Vec3 b) noexcept { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
        Vec3 Add(Vec3 a, Vec3 b) noexcept { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    }

    std::optional<Impact> ImpactTracker::Update(const ImpactSample& sample) noexcept
    {
        const float dt = std::isfinite(sample.deltaSeconds) ? std::max(sample.deltaSeconds, 0.0F) : 0.0F;
        if (!last_ || dt <= 0.0F) {
            last_ = sample.velocity;
            lastAirborne_ = sample.airborne;
            return Finish();
        }
        Vec3 expected = *last_;
        if (lastAirborne_) {
            expected.z -= settings_.gravity * dt;
        }
        const Vec3  lost = Sub(expected, sample.velocity);
        const float loss = Length(lost);
        const bool  slower = Length(sample.velocity) < Length(expected);
        last_ = sample.velocity;
        lastAirborne_ = sample.airborne;

        if (current_) {
            if (slower && loss >= settings_.continueSpeed && current_->frames < settings_.maxFrames) {
                current_->lost = Add(current_->lost, lost);
                ++current_->frames;
                return std::nullopt;
            }
            return Finish();
        }
        if (slower && loss >= settings_.startSpeed) {
            current_ = Impact{ lost, expected, 1 };
        }
        return std::nullopt;
    }

    void ImpactTracker::Reset() noexcept
    {
        last_.reset();
        current_.reset();
    }

    std::optional<Impact> ImpactTracker::Finish() noexcept
    {
        auto done = current_;
        current_.reset();
        return done;
    }

    float EquivalentFallMetres(float speedLost, float gravity) noexcept
    {
        return gravity > 0.0F ? speedLost * speedLost / (2.0F * gravity) : 0.0F;
    }
}
