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

    // Safe to read from any thread.
    struct Info
    {
        const char* status{ "idle" };
        bool        smoothCam{ false };
        float       yawDegrees{ 0.0F };   // chase yaw, world
        float       lookDegrees{ 0.0F };  // player's look-around offset
    };

    [[nodiscard]] Info GetInfo() noexcept;
}
