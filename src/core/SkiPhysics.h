#pragma once

#include "core/Vec3.h"


namespace SkiingInSkyrim::Core
{
    struct Vec2
    {
        float x{};
        float y{};
    };

    [[nodiscard]] constexpr float Dot(Vec2 a, Vec2 b) noexcept { return a.x * b.x + a.y * b.y; }
    [[nodiscard]] inline float Length(Vec2 v) noexcept { return std::hypot(v.x, v.y); }

    // Yaw convention: forward = (sin yaw, cos yaw), right = (cos yaw, -sin yaw).
    [[nodiscard]] inline Vec2 Forward(float yaw) noexcept { return { std::sin(yaw), std::cos(yaw) }; }
    [[nodiscard]] inline Vec2 Right(float yaw) noexcept { return { std::cos(yaw), -std::sin(yaw) }; }

    // One ski (both skis treated as a single one). The ski points along the character's yaw; velocity is
    // separate, and the angle between them is the slip angle. The edge pushes only sideways to the ski, up
    // to a grip limit that grows with edge angle, so a ski across the direction of travel brakes instead
    // of turning.
    //
    // Velocities are speed along the ground (m/s) pointing in a horizontal direction: the engine projects
    // the movement vector onto the slope itself.
    struct SkiSettings
    {
        float gravity{ 9.81F };
        // Kinetic friction coefficient of waxed skis sliding along their length.
        float snowFriction{ 0.05F };
        // Air drag as deceleration per (m/s)^2; sets the top speed on a given slope.
        float dragPerSpeedSquared{ 0.02F };
        float maxSpeed{ 30.0F };

        // Sidecut radius in metres. A carving ski at edge angle t follows an arc of radius sidecut * cos(t).
        float sidecutRadius{ 16.0F };
        float maxEdgeDegrees{ 55.0F };
        // Below this edge angle the ski is only partly engaged and carves a wider arc.
        float edgeEngageDegrees{ 12.0F };
        // Sideways grip as a friction coefficient: flatGrip + edgeGrip * tan(edge angle).
        float flatGrip{ 0.12F };
        float edgeGrip{ 0.8F };

        // Edge change, full range per second: analog triggers, digital buttons pressed, buttons released.
        float triggerEdgeRamp{ 8.0F };
        float buttonEdgeRamp{ 2.0F };
        float buttonEdgeRelease{ 0.6F };

        // Skid: holding the skid button pivots (twists) the skis toward the edged side, or into the hill
        // with no edge, reaching pivotMaxDegrees after pivotBuildSeconds; releasing untwists them over
        // pivotReturnSeconds. The twisted stance digs the edges in and brakes along the skis as well, so
        // holding skid brings you to a stop and holds you there.
        float pivotMaxDegrees{ 90.0F };
        float pivotBuildSeconds{ 0.3F };
        float pivotReturnSeconds{ 0.4F };
        // While moving, skid never twists the skis more than 90 degrees from the direction of travel.
        // Coming to a stop (below parkSpeed, m/s) while holding skid holds you there, and the skis stay
        // where they ended up when you let go.
        float parkSpeed{ 0.3F };

        // Steering: the stick aims the skis (relative to the camera), turning them toward the aim at up to
        // steerDegreesPerSecond with a full push; pushes inside aimDeadzone (fraction of full) do nothing.
        // Steering harder than the skis can carve skids them round, scrubbing speed.
        float steerDegreesPerSecond{ 120.0F };
        float aimDeadzone{ 0.2F };

        // Side-slipping (sliding slowly and mostly sideways), the edge's grip is measured against the slope:
        // no edge slides on flat skis, full edge holds you, and the trigger travel in between feathers the
        // slip on any slope.

        // Briefly off the ground (up to groundHugSeconds) after skiing over a crest, without jumping: the
        // skier isn't thrown upward off the slope they left and is pulled down at groundHugGravity extra
        // (m/s^2), so small crests are absorbed and only real drops give air. 0 seconds turns it off.
        float groundHugSeconds{ 0.35F };
        float groundHugGravity{ 35.0F };
        float groundHugMaxDropSpeed{ 2.0F };  // m/s the pull may add beyond following the slope
        // If the engine delivers less than this fraction of what we asked for, something is in the way.
        float blockedRatio{ 0.75F };
        // Per-frame blend toward the delivered velocity while blocked.
        float blockedBlend{ 0.3F };
        float blockedMinSpeed{ 0.5F };
    };

    struct SkiInput
    {
        float deltaSeconds{ 0.0F };
        bool  grounded{ true };
        bool  hasNormal{ false };
        Vec3  normal{ 0.0F, 0.0F, 1.0F };  // smoothed ground normal, unit length
        float yaw{ 0.0F };                 // current ski heading, radians
        float edgeTarget{ 0.0F };          // -1 left edge .. +1 right edge, fraction of max edge
        bool  edgeFromButtons{ false };    // digital input: ramp in, release slowly
        bool  skidHeld{ false };
        float aimYaw{ 0.0F };              // where the stick aims the skis, radians
        float aimStrength{ 0.0F };         // 0..1, how firmly (see AimStrength)
        bool  hasObserved{ false };
        Vec2  observed{};                  // velocity the engine actually delivered, same convention
    };

    struct SkiState
    {
        Vec2  velocity{};
        float edge{ 0.0F };           // -1 .. +1, fraction of max edge
        float pivotAngle{ 0.0F };     // radians of twist currently applied, >= 0
        float pivotSide{ 1.0F };      // +1 right, -1 left
        float lastEdgeSide{ 1.0F };   // pivot direction with flat skis on flat ground
        bool  parked{ false };        // stopped while holding skid
        float skidHold{ 0.0F };       // 0..1, how far into the skid stance (builds over pivotBuildSeconds)
        float commandedSpeed{ 0.0F };
    };

    struct SkiStep
    {
        Vec2  velocity{};
        float yaw{ 0.0F };
        bool  blocked{ false };
        float edgeDegrees{ 0.0F };     // signed, positive = right edge
        float pivotDegrees{ 0.0F };    // signed twist currently applied, positive = right
        float slipDegrees{ 0.0F };     // signed angle from the ski to the velocity, positive = sliding right
        float gripLimit{ 0.0F };       // m/s^2 of sideways force the edge can give
        bool  skidding{ false };       // sideways motion exceeded the grip limit
        bool  parked{ false };         // stopped while holding skid
    };

    // Arc radius of a carving ski at the given edge angle, or infinity when flat.
    [[nodiscard]] float CarveRadius(float edgeRadians, const SkiSettings& settings) noexcept;

    // How firmly a stick pushed to (x, y) steers, 0..1: nothing inside the deadzone, then rising gently so
    // small pushes make fine corrections.
    [[nodiscard]] float AimStrength(float stickX, float stickY, float deadzone) noexcept;

    SkiStep StepSki(SkiState& state, const SkiInput& input, const SkiSettings& settings = {}) noexcept;

    // While briefly off the ground after skiing over a crest: the vertical velocity (m/s) to use instead
    // of velocity.z, following the slope just left (lastNormal) rather than rising off it, plus the extra
    // downward pull.
    [[nodiscard]] float GroundHugVerticalSpeed(Vec3 velocity, Vec3 lastNormal, float deltaSeconds,
        const SkiSettings& settings = {}) noexcept;
}
