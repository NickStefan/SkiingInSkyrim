#include "core/Stance.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace SkiingInSkyrim::Core
{
    namespace
    {
        constexpr float kRadians = std::numbers::pi_v<float> / 180.0F;
        // No further than this either way, however hard the turn.
        constexpr float kMaxLeanRadians = 55.0F * kRadians;
        // Never quite straight, so the knee keeps a definite direction to bend.
        constexpr float kMaxReach = 0.999F;
        // After the push, seconds to bring the leg back, and how long the skating stance lasts after that
        // without another stride.
        constexpr float kRecoverSeconds = 0.3F;
        constexpr float kSkateHoldSeconds = 0.8F;

        float Smoothstep(float x)
        {
            const float t = std::clamp(x, 0.0F, 1.0F);
            return t * t * (3.0F - 2.0F * t);
        }

        Vec3 Add(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
        Vec3 Sub(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
        Vec3 Scale(Vec3 v, float s) { return { v.x * s, v.y * s, v.z * s }; }
        Vec3 Cross(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }

        Vec3 Normalized(Vec3 v, Vec3 fallback)
        {
            const float length = Length(v);
            return length > 1e-6F ? Scale(v, 1.0F / length) : fallback;
        }
    }

    StanceAngles Stance(const StanceSettings& settings, float tuck) noexcept
    {
        const float t = std::clamp(tuck, 0.0F, 1.0F);
        return {
            .ankle = (settings.ankleFlexDegrees + settings.tuckAnkleDegrees * t) * kRadians,
            .hip = (settings.hipFlexDegrees + settings.tuckHipDegrees * t) * kRadians,
            .torso = (settings.torsoLeanDegrees + settings.tuckLeanDegrees * t) * kRadians,
            .armOut = settings.armOutDegrees * kRadians,
            .armForward = (settings.armForwardDegrees + settings.tuckArmForwardDegrees * t) * kRadians,
            .elbow = settings.elbowBendDegrees * kRadians,
        };
    }

    HipShift HipShiftFor(float thighLength, float shinLength, float hipRadians, float ankleRadians) noexcept
    {
        // Hip to knee goes down and forward along the thigh; knee to ankle down and back along the shin.
        const float ankleForward = thighLength * std::sin(hipRadians) - shinLength * std::sin(ankleRadians);
        const float ankleDown = thighLength * std::cos(hipRadians) + shinLength * std::cos(ankleRadians);
        return { .forward = -ankleForward, .up = ankleDown - (thighLength + shinLength) };
    }

    TurnLean LeanFor(const StanceSettings& settings, float turnAcceleration, float gravity, float edgeRadians) noexcept
    {
        const float lean = std::atan2(turnAcceleration, std::max(gravity, 1e-3F)) * settings.turnLean;
        const float hips = lean + settings.hipAngulation * (edgeRadians - lean);
        return {
            .hips = std::clamp(hips, -kMaxLeanRadians, kMaxLeanRadians),
            .upper = std::clamp(lean * (1.0F - settings.upperBodyUpright), -kMaxLeanRadians, kMaxLeanRadians),
        };
    }

    SkatePose SkateAt(const StanceSettings& settings, const SkateStrides& strides, float pushSeconds) noexcept
    {
        SkatePose   pose;
        const float push = std::max(pushSeconds, 1e-3F);
        const int   latest = strides.side > 0.0F ? 1 : 0;
        const float seconds = strides.seconds[latest];
        if (strides.side == 0.0F || seconds < 0.0F || seconds >= push + kRecoverSeconds + kSkateHoldSeconds) {
            return pose;
        }
        pose.active = true;

        const float edge = settings.skateEdgeDegrees * kRadians;
        for (int leg = 0; leg < 2; ++leg) {
            const float t = strides.seconds[leg];
            const float pushEnds = std::min(push, strides.cut[leg]);
            if (t < 0.0F || t >= pushEnds + kRecoverSeconds) {
                continue;
            }
            auto&       legPose = pose.legs[leg];
            const float pushOut = settings.skatePushOut * Smoothstep(std::min(t, pushEnds) / push);
            const float pushEdge = edge * Smoothstep(std::min(t, pushEnds) / (push * 0.3F));
            if (t < pushEnds) {
                legPose.out = pushOut;
                legPose.edge = pushEdge;
            } else {
                const float back = (t - pushEnds) / kRecoverSeconds;
                legPose.out = pushOut * (1.0F - Smoothstep(back));
                legPose.lift = settings.skateLift * std::sin(std::numbers::pi_v<float> * back);
                legPose.edge = pushEdge * (1.0F - back);
            }
        }

        // The weight starts over the pushing ski (the last stride's glide, alternating) and moves across.
        const float s = strides.side > 0.0F ? 1.0F : -1.0F;
        const float pushed = Smoothstep(seconds / push);
        pose.hipsOver = s * (1.0F - 2.0F * pushed);
        pose.armSwing = s * (2.0F * pushed - 1.0F);
        return pose;
    }

    Leg SolveLeg(Vec3 hip, Vec3 ankleTarget, float thighLength, float shinLength, Vec3 bendToward) noexcept
    {
        const auto  toTarget = Sub(ankleTarget, hip);
        const float distance = Length(toTarget);
        const float reach = (thighLength + shinLength) * kMaxReach;
        const float shortest = std::fabs(thighLength - shinLength) + 1e-3F;
        const float d = std::clamp(distance, shortest, reach);
        const auto  along = Normalized(toTarget, { 0.0F, 0.0F, -1.0F });
        const auto  bend = Normalized(Sub(bendToward, Scale(along, Dot(bendToward, along))), { 0.0F, 1.0F, 0.0F });

        // Law of cosines for the angle at the hip between the thigh and the line to the ankle.
        const float cosine = std::clamp((thighLength * thighLength + d * d - shinLength * shinLength) / (2.0F * thighLength * d),
            -1.0F, 1.0F);
        const float sine = std::sqrt(1.0F - cosine * cosine);
        Leg leg;
        leg.knee = Add(hip, Scale(Add(Scale(along, cosine), Scale(bend, sine)), thighLength));
        leg.ankle = Add(hip, Scale(along, d));
        leg.hinge = Normalized(Cross(along, bend), { 1.0F, 0.0F, 0.0F });
        leg.excess = std::max(0.0F, distance - reach);
        return leg;
    }
}
