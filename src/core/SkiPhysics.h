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
        float dragPerSpeedSquared{ 0.01F };
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

        // Pivot (twisting the skis under you), degrees per second at full input; a fully edged ski
        // pivots at pivotEdgedFactor of that.
        float pivotDegreesPerSecond{ 120.0F };
        float pivotEdgedFactor{ 0.35F };
        float pivotRamp{ 8.0F };
        // Pivot can't turn the skis more than 90 degrees from the direction of travel while moving.
        bool limitPivot{ true };

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
        float pivotTarget{ 0.0F };         // -1 twist left .. +1 twist right
        bool  hasObserved{ false };
        Vec2  observed{};                  // velocity the engine actually delivered, same convention
    };

    struct SkiState
    {
        Vec2  velocity{};
        float edge{ 0.0F };   // -1 .. +1, fraction of max edge
        float pivot{ 0.0F };  // -1 .. +1
        float commandedSpeed{ 0.0F };
    };

    struct SkiStep
    {
        Vec2  velocity{};
        float yaw{ 0.0F };
        bool  blocked{ false };
        float edgeDegrees{ 0.0F };     // signed, positive = right edge
        float slipDegrees{ 0.0F };     // signed angle from the ski to the velocity, positive = sliding right
        float gripLimit{ 0.0F };       // m/s^2 of sideways force the edge can give
        bool  skidding{ false };       // sideways motion exceeded the grip limit
    };

    // Arc radius of a carving ski at the given edge angle, or infinity when flat.
    [[nodiscard]] float CarveRadius(float edgeRadians, const SkiSettings& settings) noexcept;

    SkiStep StepSki(SkiState& state, const SkiInput& input, const SkiSettings& settings = {}) noexcept;
}
