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
        const char*          inactiveReason{ "off" };
        Core::GroundEstimate ground;
        Core::SkiState       ski;
        Core::SkiStep        step;
        float                observedSpeed{ 0.0F };
    };

    // Main thread.
    const Debug& GetDebug();
}
