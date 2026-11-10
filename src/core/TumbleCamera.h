#pragma once

#include "core/Vec3.h"

#include <optional>

namespace SkiingInSkyrim::Core
{
    // The shot while the skier tumbles: frozen where the camera was at the hit, then drifting lazily after the
    // body so it keeps the offset it had, aimed at a smoothed point on the body (the body spins, the aim
    // doesn't), and never closer to the ground than a clearance. Positions are game units.
    struct TumbleCameraSettings
    {
        float freezeSeconds{ 0.4F };
        float followSeconds{ 1.5F };   // time constant of the drift after the body
        float lookSeconds{ 0.3F };     // time constant of the aim following the body
        float clearanceUnits{ 105.0F };
        // In the way of the view, the camera comes in at once to this far in front of what's there, and eases back
        // out with this time constant once it's clear.
        float obstructionMarginUnits{ 25.0F };
        float closestUnits{ 60.0F };
        float pullOutSeconds{ 0.6F };
    };

    struct TumbleCameraState
    {
        bool  started{ false };
        float seconds{ 0.0F };
        Vec3  camera{};  // where the shot wants the camera
        Vec3  offset{};  // camera from the body at the hit
        Vec3  look{};
        float reach{ 0.0F };  // how far from the look point the camera is shown
        Vec3  shown{};        // the camera, pulled in toward the look point when something's in the way
    };

    // Before the first update, start the shot with the camera and body where they are at the hit.
    void StartTumbleCamera(TumbleCameraState& state, Vec3 camera, Vec3 body) noexcept;

    // groundZ: the ground's height under the camera, if known.
    void UpdateTumbleCamera(TumbleCameraState& state, const TumbleCameraSettings& settings, float deltaSeconds,
        Vec3 body, std::optional<float> groundZ) noexcept;

    // After UpdateTumbleCamera(): sets shown. clearUnits: how far from the look point toward the camera is clear,
    // if something's in the way.
    void PlaceTumbleCamera(TumbleCameraState& state, const TumbleCameraSettings& settings, float deltaSeconds,
        std::optional<float> clearUnits) noexcept;

    struct LookAngles
    {
        float yaw{ 0.0F };    // radians, the game's heading: 0 along +y, positive toward +x
        float pitch{ 0.0F };  // radians, positive looking down
    };

    [[nodiscard]] LookAngles LookFrom(Vec3 from, Vec3 to) noexcept;
}
