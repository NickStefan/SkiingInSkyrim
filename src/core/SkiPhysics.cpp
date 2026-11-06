#include "core/SkiPhysics.h"

#include <algorithm>
#include <limits>
#include <numbers>

namespace SkiingInSkyrim::Core
{
    namespace
    {
        constexpr float kRadians = std::numbers::pi_v<float> / 180.0F;
        constexpr float kDegrees = 180.0F / std::numbers::pi_v<float>;

        // A per-frame slip this small is the ski carving or steering smoothly; when the edge absorbs it the
        // velocity is turned rather than shortened, as the continuous force would do. Larger slips are a
        // skid or a stop, and the edge only removes the sideways part.
        const float kSmoothSlip = std::tan(15.0F * kRadians);

        Vec2 Scale(Vec2 v, float s) noexcept { return { v.x * s, v.y * s }; }
        Vec2 Add(Vec2 a, Vec2 b) noexcept { return { a.x + b.x, a.y + b.y }; }
        Vec2 Lerp(Vec2 a, Vec2 b, float t) noexcept { return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t }; }

        float WrapAngle(float radians) noexcept
        {
            constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;
            radians = std::fmod(radians, kTwoPi);
            return radians < 0.0F ? radians + kTwoPi : radians;
        }

        float Approach(float value, float target, float maxChange) noexcept
        {
            return value + std::clamp(target - value, -maxChange, maxChange);
        }

        float EdgeRadians(float edge, const SkiSettings& settings) noexcept
        {
            return std::fabs(edge) * settings.maxEdgeDegrees * kRadians;
        }

        float Engagement(float edgeRadians, const SkiSettings& settings) noexcept
        {
            if (edgeRadians <= 0.0F) {
                return 0.0F;
            }
            const float engage = settings.edgeEngageDegrees * kRadians;
            if (engage <= 0.0F) {
                return 1.0F;
            }
            const float t = std::min(1.0F, edgeRadians / engage);
            return t * t * (3.0F - 2.0F * t);
        }

        Vec2 Decelerate(Vec2 velocity, float deceleration, float dt) noexcept
        {
            const float speed = Length(velocity);
            if (speed <= 0.0F) {
                return {};
            }
            const float newSpeed = std::max(0.0F, speed - deceleration * dt);
            return Scale(velocity, newSpeed / speed);
        }
    }

    float CarveRadius(float edgeRadians, const SkiSettings& settings) noexcept
    {
        const float engagement = Engagement(std::fabs(edgeRadians), settings);
        if (engagement <= 0.0F) {
            return std::numeric_limits<float>::infinity();
        }
        return settings.sidecutRadius * std::cos(edgeRadians) / engagement;
    }

    SkiStep StepSki(SkiState& state, const SkiInput& input, const SkiSettings& settings) noexcept
    {
        const float dt = std::max(0.0F, input.deltaSeconds);
        SkiStep result;

        const float edgeTarget = std::clamp(input.edgeTarget, -1.0F, 1.0F);
        const float edgeRate = !input.edgeFromKeys      ? settings.stickEdgeRamp :
                               edgeTarget != 0.0F       ? settings.keyEdgeRamp :
                                                          settings.keyEdgeRelease;
        state.edge = Approach(state.edge, edgeTarget, edgeRate * dt);
        state.pivot = Approach(state.pivot, std::clamp(input.pivotTarget, -1.0F, 1.0F), settings.pivotRamp * dt);

        const float edgeAngle = EdgeRadians(state.edge, settings);
        const float pivotRate = settings.pivotDegreesPerSecond * kRadians *
                                (1.0F + (settings.pivotEdgedFactor - 1.0F) * std::fabs(state.edge));
        float yaw = input.yaw + state.pivot * pivotRate * dt;

        auto velocity = state.velocity;
        if (input.hasObserved && state.commandedSpeed > settings.blockedMinSpeed &&
            Length(input.observed) < settings.blockedRatio * state.commandedSpeed) {
            velocity = Lerp(velocity, input.observed, settings.blockedBlend);
            result.blocked = true;
        }

        const float speed = Length(velocity);
        if (input.grounded) {
            const auto n = input.hasNormal ? input.normal : Vec3{ 0.0F, 0.0F, 1.0F };
            const float normalForce = settings.gravity * std::max(0.0F, n.z);
            const float grip = normalForce * (settings.flatGrip + settings.edgeGrip * std::tan(edgeAngle));
            result.gripLimit = grip;

            // The edged ski follows its arc, but only as tightly as the grip can hold at this speed.
            const float radius = CarveRadius(edgeAngle, settings);
            if (std::isfinite(radius)) {
                const float along = Dot(velocity, Forward(yaw));
                const float demand = along * along / radius;
                const float held = demand > grip ? grip / demand : 1.0F;
                yaw += std::copysign(1.0F, state.edge) * along / radius * held * dt;
            }

            const auto forward = Forward(yaw);
            const auto right = Right(yaw);
            const Vec2 downhill{ n.x * settings.gravity, n.y * settings.gravity };
            float along = Dot(velocity, forward);
            const float side = Dot(velocity, right);
            float newSide = side + Dot(downhill, right) * dt;
            const float sideLimit = grip * dt;
            if (std::fabs(newSide) <= sideLimit) {
                if (std::fabs(side) <= kSmoothSlip * std::fabs(along)) {
                    along = std::copysign(std::hypot(along, side), along);
                }
                newSide = 0.0F;
            } else {
                newSide -= std::copysign(sideLimit, newSide);
                result.skidding = true;
            }
            along += Dot(downhill, forward) * dt;
            along = std::copysign(std::max(0.0F, std::fabs(along) - settings.snowFriction * normalForce * dt), along);

            velocity = Add(Scale(forward, along), Scale(right, newSide));
            velocity = Decelerate(velocity, settings.dragPerSpeedSquared * speed * speed, dt);
        } else {
            velocity = Decelerate(velocity, settings.dragPerSpeedSquared * speed * speed, dt);
        }

        const float newSpeed = Length(velocity);
        if (newSpeed > settings.maxSpeed) {
            velocity = Scale(velocity, settings.maxSpeed / newSpeed);
        }

        yaw = WrapAngle(yaw);
        state.velocity = velocity;
        state.commandedSpeed = Length(velocity);
        result.velocity = velocity;
        result.yaw = yaw;
        result.edgeDegrees = std::copysign(edgeAngle * kDegrees, state.edge);
        if (state.commandedSpeed > 0.1F) {
            result.slipDegrees = std::atan2(Dot(velocity, Right(yaw)), Dot(velocity, Forward(yaw))) * kDegrees;
        }
        return result;
    }
}
