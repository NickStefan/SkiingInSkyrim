#pragma once

#include <span>

namespace SkiingInSkyrim::Core
{
    // One point of the landscape around the skier.
    struct SnowSample
    {
        bool snow{ false };    // the land's material there is snow
        bool packed{ false };  // a road, path or trail
    };

    // 0..1: the share of the points that are snow off roads, paths and trails.
    [[nodiscard]] float PowderCover(std::span<const SnowSample> samples) noexcept;

    // value moved toward target as the skier covers movedMetres, all the way after blendMetres.
    [[nodiscard]] float FollowOverDistance(float value, float target, float movedMetres, float blendMetres) noexcept;
}
