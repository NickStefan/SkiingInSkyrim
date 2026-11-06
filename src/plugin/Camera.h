#pragma once

#include "core/SkiPhysics.h"

namespace SkiingInSkyrim::Camera
{
    // Checks for camera mods that may interact with the chase camera; call after all plugins have loaded.
    void Connect();

    // Main thread, while skiing, around changing the player's yaw: Observe() reads the camera's world yaw
    // before the change, Apply() points the third-person camera along the direction of travel after it.
    // heading: while skidding or parked, where the skier will head afterwards; the camera looks there.
    void Observe(RE::PlayerCharacter* player);
    void Apply(RE::PlayerCharacter* player, float delta, float skiYaw, Core::Vec2 velocity,
        std::optional<float> heading);

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
