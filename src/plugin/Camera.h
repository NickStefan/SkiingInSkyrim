#pragma once

#include "core/SkiPhysics.h"

namespace SkiingInSkyrim::Camera
{
    // Checks for camera mods that may interact with the chase camera; call after all plugins have loaded.
    void Connect();

    // Main thread, while skiing, around changing the player's yaw: Observe() reads the camera's world yaw
    // before the change, Apply() points the third-person camera along the direction of travel after it.
    void Observe(RE::PlayerCharacter* player);
    void Apply(RE::PlayerCharacter* player, float delta, Core::Vec2 velocity);

    // After Observe(): the world yaw the player is looking along. In first person, or with the camera not
    // orbiting, that's the player's own yaw.
    [[nodiscard]] float ViewYaw(RE::PlayerCharacter* player);
    // Where the chase camera points by itself, leaving out the player's looking around; ViewYaw() while the
    // chase camera isn't following.
    [[nodiscard]] float ChaseYaw(RE::PlayerCharacter* player);

    // Main thread, while not skiing.
    void Reset();

    // After every plugin's post-load (Enhanced Death Cam hooks the knockdown camera then, without passing the call
    // on): lets the tumble shot place the game's knockdown (bleedout) camera, which SmoothCam leaves alone.
    void Install();

    // Main thread, at the hit: the tumble shot starts where the camera is now.
    void StartTumble();
    // Main thread, during a tumble, instead of Observe() and Apply(): while the game's knockdown camera is on, it
    // shows the tumble shot (frozen at the hit, then drifting after the body, aimed at it, above the ground),
    // through the ragdoll and getting up. Bleeding out, the shot stops once the ragdoll does. With another
    // camera on (Enhanced Death Cam's ragdoll camera without SmoothCam), there's no shot. Off with the chase camera.
    void FilmTumble(RE::PlayerCharacter* player, float delta, bool downed);
    // Main thread: ends the tumble shot, if it's on.
    void EndTumbleShot();
    // Main thread: from the game's knockdown camera to third person, looking the way the tumble shot did, if the
    // chase camera is on and it's in it.
    void LeaveKnockdownCamera(RE::PlayerCharacter* player);

    // Main thread. The game's camera right now: its state, where it is, and how far the character's position and
    // the ragdoll's root are from each other and from it.
    [[nodiscard]] std::string Describe(RE::PlayerCharacter* player);
    // Main thread. Logs the camera's state whenever it changes (and the first time, with restart), with a label.
    void WatchState(RE::PlayerCharacter* player, const char* label, bool restart = false);

    // Safe to read from any thread.
    struct Info
    {
        const char* status{ "idle" };
        bool        smoothCam{ false };
        bool        enhancedDeathCam{ false };
        bool        enhancedDeathCamRagdoll{ false };  // its ragdoll camera is on
        float       yawDegrees{ 0.0F };   // chase yaw, world
        float       lookDegrees{ 0.0F };  // player's look-around offset
    };

    [[nodiscard]] Info GetInfo() noexcept;
}
