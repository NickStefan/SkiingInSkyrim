#pragma once

#include "core/Vec3.h"

namespace SkiingInSkyrim::Core
{
    // The skier's body shape, in degrees, relative to the character's facing: legs and torso from vertical,
    // arms from hanging straight down at the sides.
    struct StanceSettings
    {
        float ankleFlexDegrees{ 20.0F };    // shins lean forward
        float hipFlexDegrees{ 30.0F };      // thighs lean forward; with the shins, bends the knees
        float torsoLeanDegrees{ 20.0F };    // upper body leans forward
        float armOutDegrees{ 15.0F };       // arms out from the sides
        float armForwardDegrees{ 25.0F };   // arms swung forward, relative to the torso
        float elbowBendDegrees{ 40.0F };
        // Added at a full tuck.
        float tuckAnkleDegrees{ 10.0F };
        float tuckHipDegrees{ 10.0F };
        float tuckLeanDegrees{ 35.0F };
        float tuckArmForwardDegrees{ 30.0F };
        // Turning: the body leans in as far as the turn's sideways force calls for, times turnLean. The
        // hips go further, by hipAngulation of whatever edge angle the lean doesn't account for (so an
        // edge held across the hill pushes the hips into it), and the upper body leans upperBodyUpright
        // less than the lean.
        float turnLean{ 0.7F };
        float hipAngulation{ 0.5F };
        float upperBodyUpright{ 0.4F };
    };

    struct StanceAngles
    {
        float ankle{ 0.0F };  // radians
        float hip{ 0.0F };
        float torso{ 0.0F };
        float armOut{ 0.0F };
        float armForward{ 0.0F };
        float elbow{ 0.0F };
    };

    [[nodiscard]] StanceAngles Stance(const StanceSettings& settings, float tuck) noexcept;

    // How far the hips move (forward, up; same units as the lengths) so that, with the thighs and shins
    // leaning by these angles, the ankles stay where they are in the standing pose.
    struct HipShift
    {
        float forward{ 0.0F };
        float up{ 0.0F };
    };
    [[nodiscard]] HipShift HipShiftFor(float thighLength, float shinLength, float hipRadians, float ankleRadians) noexcept;

    // Sideways roll (radians, positive leans right) of the hips about the feet and of the upper body, for a
    // turn pulling sideways at turnAcceleration (m/s^2, positive right) on skis edged at edgeRadians
    // (positive right edge).
    struct TurnLean
    {
        float hips{ 0.0F };
        float upper{ 0.0F };
    };
    [[nodiscard]] TurnLean LeanFor(const StanceSettings& settings, float turnAcceleration, float gravity,
        float edgeRadians) noexcept;

    // A two-bone leg from hip toward ankleTarget, the knee bending toward bendToward. If the target is out of
    // reach the leg points straight at it and excess is how far short it falls.
    struct Leg
    {
        Vec3  knee;
        Vec3  ankle;
        Vec3  hinge;  // unit axis the knee bends about: the normal of the leg's plane
        float excess{ 0.0F };
    };
    [[nodiscard]] Leg SolveLeg(Vec3 hip, Vec3 ankleTarget, float thighLength, float shinLength, Vec3 bendToward) noexcept;
}
