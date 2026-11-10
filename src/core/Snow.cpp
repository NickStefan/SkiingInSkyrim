#include "core/Snow.h"

#include <algorithm>

namespace SkiingInSkyrim::Core
{
    float PowderCover(std::span<const SnowSample> samples) noexcept
    {
        if (samples.empty()) {
            return 0.0F;
        }
        const auto powder = std::ranges::count_if(samples, [](const SnowSample& s) { return s.snow && !s.packed; });
        return static_cast<float>(powder) / static_cast<float>(samples.size());
    }

    float FollowOverDistance(float value, float target, float movedMetres, float blendMetres) noexcept
    {
        const float t = blendMetres > 0.0F ? std::clamp(movedMetres / blendMetres, 0.0F, 1.0F) : 1.0F;
        return value + (target - value) * t;
    }
}
