#pragma once

#include "core/Vec3.h"

namespace SkiingInSkyrim
{
    inline Core::Vec3 ToVec3(const RE::hkVector4& v) noexcept
    {
        alignas(16) float values[4];
        _mm_store_ps(values, v.quad);
        return { values[0], values[1], values[2] };
    }
}
