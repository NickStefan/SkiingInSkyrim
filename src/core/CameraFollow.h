#pragma once

#include "core/SkiPhysics.h"

namespace SkiingInSkyrim::Core
{
    // Chase camera yaw: follows the direction of travel with a lag, and stays put when nearly stopped (the
    // stick aims relative to the camera, so following the skis would chase the aim round), plus a
    // look-around offset from the player's own camera input that eases back once they stop looking. Yaws
    // are world radians in the ski model's convention.
    struct CameraFollowSettings
    {
        bool  enabled{ true };
        // Time constant of the follow; larger is lazier.
        float followSeconds{ 0.35F };
        // The direction of travel is averaged over about this long first, so quick back-and-forth carves
        // mostly cancel out.
        float travelSmoothingSeconds{ 0.4F };
        // Within this angle of the camera, the direction of travel only pulls it round slowly
        // (recenterSeconds), so small swings don't move it.
        float lazyZoneDegrees{ 15.0F };
        float recenterSeconds{ 1.5F };
        // After the player stops looking around, wait this long, then ease back with this time constant.
        float lookHoldSeconds{ 0.6F };
        float lookReturnSeconds{ 0.5F };
        // Below travelMinSpeed the camera holds still; by travelFullSpeed it follows at the full rate.
        float travelMinSpeed{ 0.5F };
        float travelFullSpeed{ 2.0F };
    };

    struct CameraFollowState
    {
        bool  started{ false };
        float yaw{ 0.0F };           // where the chase camera points, before the look offset
        Vec2  travel{};              // smoothed direction of travel (not unit length)
        float look{ 0.0F };          // player's look-around offset
        float sinceLook{ 0.0F };
        float lastWritten{ 0.0F };   // camera yaw we set last frame
    };

    // observedYaw: the camera's world yaw this frame before we change anything; any difference from what
    // we wrote last frame is the player's look input. Returns the camera yaw to set.
    float UpdateCameraFollow(CameraFollowState& state, const CameraFollowSettings& settings, float deltaSeconds,
        float observedYaw, Vec2 velocity) noexcept;
}
