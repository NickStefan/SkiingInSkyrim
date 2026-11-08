#pragma once

#include "core/GroundEstimate.h"
#include "core/SkiPhysics.h"

namespace SkiingInSkyrim::Skiing
{
    // Main thread, every frame after the game's own player update. Steps the ski model and sets facing.
    void Update(RE::PlayerCharacter* player, float delta);

    // From the ModifyMovementData hook, after every other hook below ours: replaces the horizontal
    // movement with the ski model's velocity while skiing is active.
    void ApplyMovement(RE::PlayerCharacter* player, RE::NiPoint3& movement, RE::NiPoint3& rotation);

    struct Debug
    {
        bool                 active{ false };
        const char*          inactiveReason{ "no skis" };
        Core::GroundEstimate ground;
        Core::SkiState       ski;
        Core::SkiStep        step;
        float                observedSpeed{ 0.0F };
        bool                 hugging{ false };  // holding the skier to the slope over a crest this frame
        float                fallMetres{ -1.0F };  // off the ground: the fall the game will count, if known
        float                heightCorrection{ 0.0F };  // game units the position was lowered this frame
    };

    // Main thread.
    const Debug& GetDebug();

    // What the debug overlay draws; directions use the model's horizontal convention.
    struct OverlayData
    {
        bool        active{ false };
        const char* inactiveReason{ "no skis" };
        bool        grounded{ true };
        float       yaw{ 0.0F };
        Core::Vec2  velocity{};
        Core::Vec2  downhill{};  // horizontal part of the ground normal
        float       slopeDegrees{ 0.0F };
        float       edgeDegrees{ 0.0F };
        float       maxEdgeDegrees{ 0.0F };
        float       pivotDegrees{ 0.0F };  // signed: positive twists toward the right edge
        float       maxPivotDegrees{ 0.0F };
        float       slipDegrees{ 0.0F };
        float       gripLimit{ 0.0F };
        bool        skidding{ false };
        bool        parked{ false };
        float       tuck{ 0.0F };  // forward lean, 0..1
        bool        skating{ false };
    };

    // Any thread.
    OverlayData GetOverlay();

    // What the body and the skis are doing, for posing them on the animation thread.
    struct Body
    {
        bool               active{ false };
        float              tuck{ 0.0F };
        float              yaw{ 0.0F };          // ski heading, world, radians
        float              edgeRadians{ 0.0F };  // signed, positive = right edge
        float              turnAcceleration{ 0.0F };  // m/s^2, smoothed, positive right
        float              gravity{ 9.81F };
        float              strideSide{ 0.0F };      // latest skating stride's leg, +1 right, -1 left, 0 none
        float              strideSeconds{ -1.0F };  // since it started
        float              slipRadians{ 0.0F };     // travel's angle from the skis, smoothed, positive right
        float              airDropMetres{ 0.0F };   // off the ground: how far below the highest point; else 0
        Core::Vec3         groundNormal{ 0.0F, 0.0F, 1.0F };  // world
        RE::TESObjectARMO* skis{ nullptr };
    };

    // Any thread.
    Body GetBody();
}
