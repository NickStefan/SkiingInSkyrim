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

        // Full edge while side-slipping grips this many times the slope's pull, so it stops a slip as well as
        // holding.
        constexpr float kFeatherHoldMargin = 1.6F;

        // Side-slip feathering (see FeatherGrip): how sideways (sine of the angle between skis and slide) and
        // how slow (m/s) the slide has to be, and the speed below which the slide's direction is taken from
        // the slope rather than the velocity.
        constexpr float kFeatherSidewaysStart = 0.5F;
        constexpr float kFeatherSidewaysFull = 0.87F;
        constexpr float kFeatherFullSpeed = 3.0F;
        constexpr float kFeatherNoneSpeed = 6.0F;
        constexpr float kFeatherDirectionSpeed = 0.3F;
        // The feathering edge always reaches at least this (m/s^2), so a slow sideways slide on gentle or flat
        // ground can be stopped.
        constexpr float kFeatherMinFullGrip = 3.0F;
        // The trigger feathers over this much of its travel; past it, the edge digs in up to its usual grip,
        // so squeezing all the way always stops you hard.
        constexpr float kFeatherTravel = 0.8F;

        // Self-aligning (see SkiSettings::selfAlignDegreesPerSecond) fades in over the same speeds the
        // side-slip feathering fades out, and only with the edge within kEdgeSideThreshold of flat.
        constexpr float kAlignStartSpeed = kFeatherFullSpeed;
        constexpr float kAlignFullSpeed = kFeatherNoneSpeed;

        // A trigger squeezed past this (fraction of full edge) from below it is a skating stride.
        constexpr float kSkateSqueeze = 0.5F;
        // A mouse-button stride only edges this far (fraction of full edge) and releases at trigger speed,
        // so the skis glide between strides instead of carving.
        constexpr float kSkateButtonEdge = 0.25F;
        // Skating needs the stick pushed at least this firmly within kSkateAimDegrees of the skis.
        constexpr float kSkateAimStrength = 0.3F;
        constexpr float kSkateAimDegrees = 45.0F;

        // Tucking: how firm a push (aim strength) and how close to where the skis point (cosine of the angle).
        constexpr float kTuckStartStrength = 0.5F;
        constexpr float kTuckFullStrength = 0.9F;
        const float     kTuckStartAlign = std::cos(30.0F * kRadians);
        const float     kTuckFullAlign = std::cos(10.0F * kRadians);

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

        float Approach(float value, float target, float maxChange) noexcept
        {
            return value + std::clamp(target - value, -maxChange, maxChange);
        }

        float RotateToward(float yaw, float target, float maxChange) noexcept
        {
            return yaw + std::clamp(WrapSigned(target - yaw), -maxChange, maxChange);
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

        float FlatGrip(const SkiSettings& settings) noexcept
        {
            return settings.flatGrip * (settings.powder ? settings.powderSideGrip : 1.0F);
        }

        float Smoothstep(float edge0, float edge1, float x) noexcept
        {
            const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0F, 1.0F);
            return t * t * (3.0F - 2.0F * t);
        }

        // Sliding slowly and mostly sideways (a side-slip), the edge's grip is measured against the slope
        // rather than the usual edge grip: no edge slides on flat skis, kFeatherTravel of edge grips
        // kFeatherHoldMargin times the slope's pull, with grip following the square of the edge in between,
        // so the trigger feathers the slip on any slope; beyond that it digs in to the usual grip. Only the
        // uphill edge, the one against the slide, grips. Carving and fast skids keep the usual grip.
        float FeatherGrip(float edgeGrip, float edge, float yaw, Vec2 velocity, Vec3 normal, float normalForce,
            const SkiSettings& settings) noexcept
        {
            const Vec2  downhill{ normal.x, normal.y };
            const float speed = Length(velocity);
            const Vec2  slide = speed >= kFeatherDirectionSpeed ? velocity : downhill;
            const float slideLength = Length(slide);
            if (slideLength < 1e-4F) {
                return edgeGrip;
            }
            const float side = Dot(slide, Right(yaw)) / slideLength;
            const float weight = Smoothstep(kFeatherSidewaysStart, kFeatherSidewaysFull, std::fabs(side)) *
                                 (1.0F - Smoothstep(kFeatherFullSpeed, kFeatherNoneSpeed, speed));
            if (weight <= 0.0F) {
                return edgeGrip;
            }
            const float flat = normalForce * FlatGrip(settings);
            const float hold = std::max(kFeatherHoldMargin * settings.gravity * Length(downhill), kFeatherMinFullGrip);
            const float uphillEdge = std::max(0.0F, -edge * std::copysign(1.0F, side));
            float       feather = 0.0F;
            if (uphillEdge <= kFeatherTravel) {
                const float t = uphillEdge / kFeatherTravel;
                feather = flat + t * t * std::max(0.0F, hold - flat);
            } else {
                const float t = (uphillEdge - kFeatherTravel) / (1.0F - kFeatherTravel);
                feather = std::max(flat, hold) + t * std::max(0.0F, edgeGrip - std::max(flat, hold));
            }
            return edgeGrip + (feather - edgeGrip) * weight;
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

    float GroundHugVerticalSpeed(Vec3 velocity, Vec3 lastNormal, float deltaSeconds, const SkiSettings& settings) noexcept
    {
        float vertical = velocity.z;
        float floor = -std::numeric_limits<float>::infinity();
        if (lastNormal.z > 0.1F) {
            const float alongSlope = -(lastNormal.x * velocity.x + lastNormal.y * velocity.y) / lastNormal.z;
            vertical = std::min(vertical, alongSlope);
            // The pull mustn't drive you into the slope much faster than following it: landing like that
            // is a jolt. Gravity alone can still take you faster, off a real drop.
            floor = std::min(vertical, alongSlope - settings.groundHugMaxDropSpeed);
        }
        return std::max(floor, vertical - settings.groundHugGravity * std::max(0.0F, deltaSeconds));
    }

    float ImpactHeight(Vec3 velocity, Vec3 slopeNormal, float gravity) noexcept
    {
        const float length = std::sqrt(slopeNormal.x * slopeNormal.x + slopeNormal.y * slopeNormal.y +
                                       slopeNormal.z * slopeNormal.z);
        if (length < 1e-4F || gravity <= 0.0F) {
            return 0.0F;
        }
        const float into = -(velocity.x * slopeNormal.x + velocity.y * slopeNormal.y + velocity.z * slopeNormal.z) / length;
        return into > 0.0F ? into * into / (2.0F * gravity) : 0.0F;
    }

    float LandingHeight(Vec3 velocity, Vec3 slopeNormal, const SkiSettings& settings) noexcept
    {
        const float height = ImpactHeight(velocity, slopeNormal, settings.gravity);
        return settings.powder ? height * (1.0F - std::clamp(settings.powderLanding, 0.0F, 1.0F)) : height;
    }

    float AimStrength(float stickX, float stickY, float deadzone) noexcept
    {
        const float push = std::min(1.0F, std::hypot(stickX, stickY));
        if (push <= deadzone || deadzone >= 1.0F) {
            return 0.0F;
        }
        const float t = (push - deadzone) / (1.0F - deadzone);
        return t * t;
    }

    SkiStep StepSki(SkiState& state, const SkiInput& input, const SkiSettings& settings) noexcept
    {
        const float dt = std::max(0.0F, input.deltaSeconds);
        SkiStep result;

        const float edgeTarget = std::clamp(input.edgeTarget, -1.0F, 1.0F);
        const bool  skateEdge = input.edgeFromButtons && state.skateEdging;
        const float edgeRate = !input.edgeFromButtons || skateEdge ? settings.triggerEdgeRamp :
                               edgeTarget != 0.0F                  ? settings.buttonEdgeRamp :
                                                                     settings.buttonEdgeRelease;
        const float edgeGoal = skateEdge ? std::clamp(edgeTarget, -kSkateButtonEdge, kSkateButtonEdge) : edgeTarget;
        state.edge = Approach(state.edge, edgeGoal, edgeRate * dt);
        if (!input.edgeFromButtons || (edgeTarget == 0.0F && state.edge == 0.0F)) {
            state.skateEdging = false;
        }
        if (std::fabs(state.edge) > kEdgeSideThreshold) {
            state.lastEdgeSide = std::copysign(1.0F, state.edge);
        }

        // Skid twists the skis toward the edged side (or into the hill) while held, up to pivotMaxDegrees,
        // and untwists the same amount on release. Only the twist actually applied is undone, so the limit
        // and carving in between don't make the return overshoot. Coming to a stop while skidding keeps the
        // skis where they are: the twist is done, and releasing leaves them pointing wherever they ended up.
        const float pivotMax = settings.pivotMaxDegrees * kRadians;
        const float buildRate = pivotMax / std::max(settings.pivotBuildSeconds, kMinSeconds);
        const float across = std::numbers::pi_v<float> / 2.0F;
        const float speedBefore = Length(state.velocity);
        float       yaw = input.yaw;
        state.skidHold = input.skidHeld ? std::min(1.0F, state.skidHold + dt / std::max(settings.pivotBuildSeconds, kMinSeconds)) :
                                          0.0F;
        if (input.skidHeld) {
            if (!state.parked) {
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
                if (input.grounded && speedBefore < settings.parkSpeed) {
                    state.parked = true;
                    state.pivotAngle = 0.0F;
                }
            }
        } else {
            state.parked = false;
            if (state.pivotAngle > 0.0F) {
                const float release = pivotMax / std::max(settings.pivotReturnSeconds, kMinSeconds) * dt;
                const float untwist = std::min(release, state.pivotAngle);
                yaw -= state.pivotSide * untwist;
                state.pivotAngle -= untwist;
            }
        }

        // Steering: the skis turn toward where the stick aims, as firmly as it's pushed, or directly by the
        // mouse. Pushing firmly the way they already point tucks.
        const float aimStrength = std::clamp(input.aimStrength, 0.0F, 1.0F);
        float       tuck = 0.0F;
        const float maxTurn = settings.steerDegreesPerSecond * kRadians * dt;
        yaw += std::clamp(input.yawTurn, -maxTurn, maxTurn);
        if (aimStrength > 0.0F) {
            tuck = Smoothstep(kTuckStartStrength, kTuckFullStrength, aimStrength) *
                   Smoothstep(kTuckStartAlign, kTuckFullAlign, std::cos(input.aimYaw - yaw));
            yaw = RotateToward(yaw, input.aimYaw, aimStrength * settings.steerDegreesPerSecond * kRadians * dt);
        } else if (input.yawTurn == 0.0F && !input.skidHeld && input.grounded && state.pivotAngle <= 0.0F &&
                   std::fabs(state.edge) <= kEdgeSideThreshold && settings.selfAlignDegreesPerSecond > 0.0F) {
            const float weight = Smoothstep(kAlignStartSpeed, kAlignFullSpeed, speedBefore);
            if (weight > 0.0F) {
                const float travel = std::atan2(state.velocity.x, state.velocity.y);
                yaw = RotateToward(yaw, travel, weight * settings.selfAlignDegreesPerSecond * kRadians * dt);
            }
        }

        const float edgeAngle = EdgeRadians(state.edge, settings);
        // While skidding, the stance digs the edges in as it builds, whatever the edge input or how far
        // the skis could twist, and the same grip brakes along the skis.
        const float stance = !input.skidHeld ? 0.0F : state.parked ? 1.0F : state.skidHold;
        const float gripAngle = std::max(edgeAngle, stance * settings.maxEdgeDegrees * kRadians);

        auto velocity = state.velocity;
        if (input.hasObserved && state.commandedSpeed > settings.blockedMinSpeed &&
            Length(input.observed) < settings.blockedRatio * state.commandedSpeed) {
            velocity = Lerp(velocity, input.observed, settings.blockedBlend);
            result.blocked = true;
        }

        // Skating: a fresh squeeze of a trigger with the stick pushed forward on gentle ground is a stride.
        const bool squeezed = std::fabs(edgeTarget) >= kSkateSqueeze && std::fabs(state.lastEdgeTarget) < kSkateSqueeze;
        state.lastEdgeTarget = edgeTarget;
        if (squeezed && input.grounded && !input.skidHeld && aimStrength >= kSkateAimStrength &&
            std::cos(input.aimYaw - yaw) >= std::cos(kSkateAimDegrees * kRadians) && settings.skateMaxSpeed > 0.0F) {
            const auto  n = input.hasNormal ? input.normal : Vec3{ 0.0F, 0.0F, 1.0F };
            const float speedNow = Length(velocity);
            if (n.z >= std::cos(settings.skateMaxSlopeDegrees * kRadians) && speedNow < settings.skateMaxSpeed) {
                const float side = std::copysign(1.0F, edgeTarget);
                const float alternate = side != state.lastSkateSide ? 1.0F : 0.5F;
                const float push = settings.skateImpulse * alternate * (1.0F - speedNow / settings.skateMaxSpeed);
                velocity = Add(velocity, Scale(Forward(yaw), push));
                state.lastSkateSide = side;
                state.skateEdging = input.edgeFromButtons;
                result.skated = true;
            }
        }

        // Above the cap the v^4 terms would brake past it in a single frame.
        const float speed = std::min(Length(velocity), settings.maxSpeed);
        const float drag = (settings.dragPerSpeedSquared + settings.dragPerSpeedFourth * speed * speed) *
                           (1.0F - std::clamp(settings.tuckDragReduction, 0.0F, 1.0F) * tuck);
        const float powderDrag = settings.powder && input.grounded ? settings.powderDragPerSpeedFourth * speed * speed : 0.0F;
        if (input.grounded) {
            const auto n = input.hasNormal ? input.normal : Vec3{ 0.0F, 0.0F, 1.0F };
            const float normalForce = settings.gravity * std::max(0.0F, n.z);
            float grip = normalForce * (FlatGrip(settings) + settings.edgeGrip * std::tan(gripAngle));
            if (!input.skidHeld) {
                grip = FeatherGrip(grip, state.edge, yaw, velocity, n, normalForce, settings);
            }
            result.gripLimit = grip;

            // The edged ski follows its arc, but only as tightly as the grip can hold at this speed. Held
            // cleanly, the carve pumps you forward.
            const float radius = CarveRadius(edgeAngle, settings);
            float       pump = 0.0F;
            if (std::isfinite(radius)) {
                const float along = std::max(0.0F, Dot(velocity, Forward(yaw)));  // skis don't carve backwards
                const float demand = along * along / radius;
                const float held = demand > grip ? grip / demand : 1.0F;
                yaw += std::copysign(1.0F, state.edge) * along / radius * held * dt;
                if (held >= 1.0F && !input.skidHeld) {
                    pump = std::min(std::max(0.0F, settings.carvePump) * demand, (drag + powderDrag) * speed * speed);
                }
            }

            // Moving, the skis never point more than across the direction of travel: when a carve turns them
            // faster than the grip can turn you, or gravity swings you round, they skid sideways rather than
            // ending up skiing backwards, and coming to a stop they end up across the hill rather than
            // pointing up it. Skis already past it come back gradually.
            if (speed >= std::max(settings.parkSpeed, 0.05F)) {
                const float travel = std::atan2(velocity.x, velocity.y);
                const float offset = WrapSigned(yaw - travel);
                if (std::fabs(offset) > across) {
                    yaw = RotateToward(yaw, travel + std::copysign(across, offset), buildRate * dt);
                }
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
            if (pump > 0.0F && !result.skidding && along > 0.0F) {
                along += pump * dt;
                result.pump = pump;
            }
            const float brake = stance * grip * dt;
            along = std::fabs(along) <= brake ? 0.0F : along - std::copysign(brake, along);
            along = std::copysign(std::max(0.0F, std::fabs(along) - settings.snowFriction * normalForce * dt), along);

            velocity = Add(Scale(forward, along), Scale(right, newSide));
            velocity = Decelerate(velocity, (drag + powderDrag) * speed * speed, dt);
        } else {
            velocity = Decelerate(velocity, drag * speed * speed, dt);
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
        result.tuck = tuck;
        if (state.pivotAngle > 0.0F) {
            result.pivotDegrees = state.pivotSide * state.pivotAngle * kDegrees;
        }
        if (state.commandedSpeed > 0.1F) {
            result.slipDegrees = std::atan2(Dot(velocity, Right(yaw)), Dot(velocity, Forward(yaw))) * kDegrees;
        }
        return result;
    }
}
