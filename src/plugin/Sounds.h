#pragma once

#include "core/SkiSound.h"

namespace SkiingInSkyrim::Sounds
{
    // At plugin load: the hook that keeps the player's footsteps quiet while skiing.
    void Install();

    // What the skis are on, for picking sounds.
    enum class Ground
    {
        Snow,
        Ice,
        Stone,
        Wood,
        Gravel,
        Grass,
        Dirt,
    };

    [[nodiscard]] Ground GroundOf(RE::MATERIAL_ID underfoot, RE::MATERIAL_ID land);
    [[nodiscard]] const char* GroundName(Ground ground);

    // Main thread, every frame while skiing is active. crashed: what a crash this frame hit.
    void Update(RE::PlayerCharacter* player, const Core::SkiSoundInput& input, Ground ground, Ground crashed);
    // Main thread, while it isn't: lets the footsteps back.
    void Stop();

    // Main thread: the flow bar has just filled or emptied.
    void PlayFlow(RE::PlayerCharacter* player);

    // Main thread. The game plays Slow Time's droning loop while the player is ragdolled; muted from a tumble's
    // start (before the ragdoll) until it's over, so the shout itself still sounds at any other time.
    void MuteRagdollLoop(bool muted);

    struct Info
    {
        bool        hooked{ false };
        bool        silencing{ false };
        const char* ground{ "" };
    };

    // Any thread.
    Info GetInfo();
}
