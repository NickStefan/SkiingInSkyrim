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
        // Drag as deceleration: dragPerSpeedSquared * v^2 + dragPerSpeedFourth * v^4. The second part is a
        // speed limiter, next to nothing at cruising speeds and steep at high ones, so gentle slopes can carry
        // speed while steep ones don't run away. Together they set the top speed on a given slope.
        float dragPerSpeedSquared{ 0.004F };
        float dragPerSpeedFourth{ 0.00005F };
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
        // Pushing firmly the way the skis already point tucks, cutting air drag by this fraction.
        float tuckDragReduction{ 0.6F };
        // A clean carve (the edge holding, not skidding) pumps: a forward push of carvePump times the turn's
        // sideways acceleration, so holding a carve through a turn keeps or builds speed. At
        // most the drag at that moment, so carving can cancel drag but never snow friction.
        float carvePump{ 0.15F };

        // Skating: on gentle ground (up to skateMaxSlopeDegrees) with the stick pushed forward, each fresh
        // squeeze of a trigger is a skating stride, pushing you along the skis by up to skateImpulse (m/s),
        // less the closer you are to skateMaxSpeed, and half as much if it's the same side as the last stride.
        float skateImpulse{ 1.0F };
        float skateMaxSpeed{ 6.0F };
        float skateMaxSlopeDegrees{ 10.0F };
        // With the stick centred, no edge and no skid, flat skis sliding faster than a side-slip swing round
        // toward the direction of travel at up to this rate, so a sideways smear doesn't go on forever.
        // Slow side-slips (falling leaf) are left alone. 0 turns it off.
        float selfAlignDegreesPerSecond{ 45.0F };

        // Side-slipping (sliding slowly and mostly sideways), the edge's grip is measured against the slope:
        // no edge slides on flat skis, full edge holds you, and the trigger travel in between feathers the
        // slip on any slope.

        // Powder: deep snow resists you more the faster you plough through it (powderDragPerSpeedFourth * v^4
        // on top of drag, so it mostly bites at speed on steep runs and leaves gentle cruising alone),
        // pushes back harder against the skis' sides (flat grip times powderSideGrip, so skids, stops and
        // steered turns bite more), and cushions landings (powderLanding of the impact absorbed).
        bool  powder{ true };
        float powderDragPerSpeedFourth{ 0.000056F };
        float powderSideGrip{ 2.0F };
        float powderLanding{ 0.5F };

        // Briefly off the ground (up to groundHugSeconds) after skiing over a crest, without jumping: the
        // skier isn't thrown upward off the slope they left and is pulled down at groundHugGravity extra
        // (m/s^2), so small crests are absorbed and only real drops give air. 0 seconds turns it off.
        float groundHugSeconds{ 0.35F };
        float groundHugGravity{ 35.0F };
        float groundHugMaxDropSpeed{ 2.0F };  // m/s the pull may add beyond following the slope
        // Keep the character at the physics body's height every frame. Descending steep slopes the game lets the
        // character's position fall behind the body and catches it up every few frames, which shows as a
        // vertical jitter.
        bool smoothHeight{ true };

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
        // 0..1, how firmly the player pushes whatever the direction, if more than aimStrength. Keys keep
        // building it while held, even as which keys changes, where aimStrength starts over.
        float pushStrength{ 0.0F };
        float yawTurn{ 0.0F };             // radians to turn the skis directly this frame (mouse steering)
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
        float lastEdgeTarget{ 0.0F }; // last frame's edge input, to see fresh trigger squeezes
        float lastSkateSide{ 0.0F };  // side of the last skating stride, 0 before the first
        bool  skateEdging{ false };   // a mouse-button stride is held: light edge, quick release
        float tuck{ 0.0F };           // 0..1
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
        float tuck{ 0.0F };            // 0..1
        float pump{ 0.0F };            // m/s^2 of forward push from carving
        float turnAcceleration{ 0.0F };  // m/s^2 the path curves at, positive right; on the ground only
        bool  skated{ false };         // a skating stride this frame
    };

    // Arc radius of a carving ski at the given edge angle, or infinity when flat.
    [[nodiscard]] float CarveRadius(float edgeRadians, const SkiSettings& settings) noexcept;

    // How firmly a stick pushed to (x, y) steers, 0..1: nothing inside the deadzone, then rising gently so
    // small pushes make fine corrections.
    [[nodiscard]] float AimStrength(float stickX, float stickY, float deadzone) noexcept;

    SkiStep StepSki(SkiState& state, const SkiInput& input, const SkiSettings& settings = {}) noexcept;

    // Metres of free fall that would land as hard as meeting a slope with the given normal at this velocity
    // (m/s): only the speed into the slope counts, so landing along it is soft whatever the drop.
    [[nodiscard]] float ImpactHeight(Vec3 velocity, Vec3 slopeNormal, float gravity) noexcept;

    // ImpactHeight, cushioned by powder if it's on.
    [[nodiscard]] float LandingHeight(Vec3 velocity, Vec3 slopeNormal, const SkiSettings& settings) noexcept;

    // While briefly off the ground after skiing over a crest: the vertical velocity (m/s) to use instead
    // of velocity.z, following the slope just left (lastNormal) rather than rising off it, plus the extra
    // downward pull.
    [[nodiscard]] float GroundHugVerticalSpeed(Vec3 velocity, Vec3 lastNormal, float deltaSeconds,
        const SkiSettings& settings = {}) noexcept;
}
