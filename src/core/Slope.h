#pragma once

#include "core/Vec3.h"

namespace SkiingInSkyrim::Core
{
    struct SlopeInfo
    {
        bool  valid{ false };
        float angleDegrees{ 0.0F };
        // Unit horizontal direction gravity pulls along the surface; zero on flat ground.
        float downhillX{ 0.0F };
        float downhillY{ 0.0F };
        // Signed angle from the facing direction to downhill; positive means downhill is to the right.
        float fallLineOffsetDegrees{ 0.0F };
        // Fraction of gravity acting along the surface (sin of the slope angle).
        float gravityAlongSlope{ 0.0F };
    };

    // yawRadians uses Skyrim's convention: 0 faces +Y (north), increasing clockwise toward +X.
    [[nodiscard]] SlopeInfo AnalyzeSlope(Vec3 surfaceNormal, float yawRadians) noexcept;
}
