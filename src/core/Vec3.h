#pragma once

#include <cmath>

namespace SkiingInSkyrim::Core
{
    struct Vec3
    {
        float x{};
        float y{};
        float z{};
    };

    [[nodiscard]] constexpr float Dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
    [[nodiscard]] inline float Length(Vec3 v) noexcept { return std::sqrt(Dot(v, v)); }
    [[nodiscard]] inline float HorizontalLength(Vec3 v) noexcept { return std::hypot(v.x, v.y); }
}
