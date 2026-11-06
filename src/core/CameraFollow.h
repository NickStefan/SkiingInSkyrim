#pragma once

#include "core/SkiPhysics.h"

#include <optional>

namespace SkiingInSkyrim::Core
{
    // Chase camera yaw: follows the direction of travel (the skis' heading when nearly stopped) with a
    // lag, plus a look-around offset from the player's own camera input that eases back once they stop
    // looking. Yaws are world radians in the ski model's convention.
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
        // Between these speeds the reference blends from the skis' heading to the direction of travel.
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
    // we wrote last frame is the player's look input. heading: while skidding or parked, where the skier
    // will be heading once it's over (SkiStep::nextHeading); the camera follows that instead of the skis
    // or the stop. Returns the camera yaw to set.
    float UpdateCameraFollow(CameraFollowState& state, const CameraFollowSettings& settings, float deltaSeconds,
        float observedYaw, float skiYaw, Vec2 velocity, std::optional<float> heading = std::nullopt) noexcept;
}
