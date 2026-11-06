#include "core/SkiPhysics.h"

#include <algorithm>
#include <limits>
#include <numbers>
#include <optional>

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

        // Below this the direction of travel is too uncertain to limit against, and the skier needs to
        // be able to turn around.
        constexpr float kPivotLimitMinSpeed = 1.0F;

        // Edge (fraction of max) beyond which it decides which way pivot twists.
        constexpr float kEdgeSideThreshold = 0.05F;

        // With no edge, skid twists into the hill when the slope (sine of its angle) is at least this and the
        // fall line is this far (sine of the angle) to one side of the skis; otherwise toward the last edge.
        constexpr float kUphillMinSlope = 0.05F;
        constexpr float kUphillMinSide = 0.25F;

        constexpr float kMinSeconds = 1e-3F;

        float SkidSide(const SkiState& state, const SkiInput& input, float edgeTarget) noexcept
        {
            if (std::fabs(edgeTarget) > kEdgeSideThreshold) {
                return std::copysign(1.0F, edgeTarget);
            }
            if (input.grounded && input.hasNormal) {
                const Vec2  downhill{ input.normal.x, input.normal.y };
                const float slope = Length(downhill);
                const float side = Dot(downhill, Right(input.yaw));
                if (slope >= kUphillMinSlope && std::fabs(side) >= kUphillMinSide * slope) {
                    return side > 0.0F ? -1.0F : 1.0F;
                }
            }
            return state.lastEdgeSide;
        }

        // Signed angle in (-pi, pi].
        float WrapSigned(float radians) noexcept
        {
            constexpr float kPi = std::numbers::pi_v<float>;
            radians = std::fmod(radians + kPi, 2.0F * kPi);
            return (radians < 0.0F ? radians + 2.0F * kPi : radians) - kPi;
        }

        // Keeps pivot from taking the skis past 90 degrees from the direction of travel. Skis already
        // beyond it (after a jump, say) are not snapped back; pivot just can't push them further.
        float LimitPivot(float before, float after, Vec2 velocity) noexcept
        {
            const float travel = std::atan2(velocity.x, velocity.y);
            const float slipBefore = WrapSigned(before - travel);
            const float slipAfter = WrapSigned(after - travel);
            const float allowed = std::max(std::fabs(slipBefore), std::numbers::pi_v<float> / 2.0F);
            if (std::fabs(slipAfter) <= allowed) {
                return after;
            }
            return travel + std::copysign(allowed, slipAfter);
        }

        float RotateToward(float yaw, float target, float maxChange) noexcept
        {
            return yaw + std::clamp(WrapSigned(target - yaw), -maxChange, maxChange);
        }

        // The heading across the fall line nearest to yaw.
        float NearestAcross(float yaw, float fallLine) noexcept
        {
            const float right = fallLine + std::numbers::pi_v<float> / 2.0F;
            const float left = fallLine - std::numbers::pi_v<float> / 2.0F;
            return std::fabs(WrapSigned(right - yaw)) <= std::fabs(WrapSigned(left - yaw)) ? right : left;
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
        const float edgeRate = !input.edgeFromButtons ? settings.triggerEdgeRamp :
                               edgeTarget != 0.0F     ? settings.buttonEdgeRamp :
                                                        settings.buttonEdgeRelease;
        state.edge = Approach(state.edge, edgeTarget, edgeRate * dt);
        if (std::fabs(state.edge) > kEdgeSideThreshold) {
            state.lastEdgeSide = std::copysign(1.0F, state.edge);
        }

        // Downhill heading, if the ground is steep enough to have one.
        std::optional<float> fallLine;
        if (input.grounded && input.hasNormal && Length(Vec2{ input.normal.x, input.normal.y }) >= kUphillMinSlope) {
            fallLine = std::atan2(input.normal.x, input.normal.y);
        }

        // Skid twists the skis toward the edged side (or into the hill) while held, up to pivotMaxDegrees,
        // and untwists the same amount on release. Only the twist actually applied is undone, so the limit
        // and carving in between don't make the return overshoot.
        //
        // Stopping while skidding parks you: the skis settle across the hill, and after release you
        // side-slip (for as long as any edge is held, or sideslipSeconds without) before they turn to point
        // downhill, or back to where you were heading on flat ground.
        const float pivotMax = settings.pivotMaxDegrees * kRadians;
        const float buildRate = pivotMax / std::max(settings.pivotBuildSeconds, kMinSeconds);
        const float speedBefore = Length(state.velocity);
        float       yaw = input.yaw;
        if (input.skidHeld) {
            state.sideslipTime = 0.0F;
            if (state.parked && fallLine) {
                yaw = RotateToward(yaw, NearestAcross(yaw, *fallLine), buildRate * dt);
            } else {
                if (state.pivotAngle <= 0.0F) {
                    state.pivotSide = SkidSide(state, input, edgeTarget);
                }
                const float twist = std::min(buildRate * dt, pivotMax - state.pivotAngle);
                float       twisted = yaw + state.pivotSide * twist;
                if (speedBefore >= kPivotLimitMinSpeed) {
                    twisted = LimitPivot(yaw, twisted, state.velocity);
                }
                const float applied = std::max(0.0F, WrapSigned(twisted - yaw) * state.pivotSide);
                yaw += state.pivotSide * applied;
                state.pivotAngle += applied;
            }
            if (!state.parked && input.grounded && speedBefore < settings.parkSpeed) {
                state.parked = true;
                state.parkHeading = WrapAngle(yaw - state.pivotSide * state.pivotAngle);
            }
        } else if (state.parked && std::fabs(input.stepTurn) > 0.0F && speedBefore < kPivotLimitMinSpeed) {
            // Turning on the spot keeps the skis where they are and ends the side-slip.
            state.parked = false;
            state.pivotAngle = 0.0F;
        } else if (state.parked) {
            if (std::fabs(state.edge) <= kEdgeSideThreshold) {
                state.sideslipTime += dt;
            }
            const float turnRate = pivotMax / std::max(settings.sideslipTurnSeconds, kMinSeconds);
            if (state.sideslipTime < settings.sideslipSeconds) {
                if (fallLine) {
                    yaw = RotateToward(yaw, NearestAcross(yaw, *fallLine), buildRate * dt);
                }
            } else {
                const float target = fallLine ? *fallLine : state.parkHeading;
                yaw = RotateToward(yaw, target, turnRate * dt);
                if (std::fabs(WrapSigned(target - yaw)) < 1e-4F) {
                    state.parked = false;
                    state.pivotAngle = 0.0F;
                }
            }
        } else if (state.pivotAngle > 0.0F) {
            const float release = pivotMax / std::max(settings.pivotReturnSeconds, kMinSeconds) * dt;
            const float untwist = std::min(release, state.pivotAngle);
            yaw -= state.pivotSide * untwist;
            state.pivotAngle -= untwist;
        }
        if (Length(state.velocity) < kPivotLimitMinSpeed) {
            yaw += std::clamp(input.stepTurn, -1.0F, 1.0F) * settings.stepTurnDegreesPerSecond * kRadians * dt;
        }

        const float edgeAngle = EdgeRadians(state.edge, settings);
        // While skidding, the twisted stance digs the edges in by as much as it's twisted, whatever the
        // edge input, and the same grip brakes along the skis.
        const float stance = !input.skidHeld ? 0.0F :
                             state.parked    ? 1.0F :
                             pivotMax > 0.0F ? std::min(1.0F, state.pivotAngle / pivotMax) :
                                               0.0F;
        const float gripAngle = std::max(edgeAngle, stance * settings.maxEdgeDegrees * kRadians);

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
            const float grip = normalForce * (settings.flatGrip + settings.edgeGrip * std::tan(gripAngle));
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
            const float brake = stance * grip * dt;
            along = std::fabs(along) <= brake ? 0.0F : along - std::copysign(brake, along);
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
        result.parked = state.parked;
        if (state.parked) {
            result.nextHeading = fallLine ? *fallLine : state.parkHeading;
            result.pivotDegrees = WrapSigned(yaw - *result.nextHeading) * kDegrees;
        } else if (state.pivotAngle > 0.0F) {
            result.nextHeading = WrapAngle(yaw - state.pivotSide * state.pivotAngle);
            result.pivotDegrees = state.pivotSide * state.pivotAngle * kDegrees;
        }
        if (state.commandedSpeed > 0.1F) {
            result.slipDegrees = std::atan2(Dot(velocity, Right(yaw)), Dot(velocity, Forward(yaw))) * kDegrees;
        }
        return result;
    }
}
