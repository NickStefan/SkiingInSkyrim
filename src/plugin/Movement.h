#pragma once

#include "core/Vec3.h"

namespace SkiingInSkyrim::Movement
{
    // Main thread, every player update. Installs the PlayerCharacter::ModifyMovementData hook on the first
    // in-game frame, after other plugins have installed theirs at startup, then watches who owns the slot.
    void Update();

    struct Sample
    {
        Core::Vec3 movement{};
        Core::Vec3 rotation{};
    };

    // Latest ModifyMovementData output, after every hook below ours.
    Sample Last();

    // ModifyMovementData calls since the last TakeCalls(), and the delta the latest was given.
    struct Calls
    {
        int   count{ 0 };
        float delta{ 0.0F };
    };
    Calls TakeCalls();

    struct ChainInfo
    {
        bool        installed{ false };
        bool        outermost{ false };  // ours runs last, so our movement is what the game uses
        std::string runsAfterUs;         // module owning the slot when we are not outermost
        std::string runsBeforeUs;        // module we call into
        bool        iceIsSlickLoaded{ false };
    };

    // Safe from any thread.
    ChainInfo GetChainInfo();
}
