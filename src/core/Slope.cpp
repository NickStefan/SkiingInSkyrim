#include "core/Slope.h"

#include <algorithm>
#include <numbers>

namespace SkiingInSkyrim::Core
{
    namespace
    {
        constexpr float kDegrees = 180.0F / std::numbers::pi_v<float>;
        constexpr float kFlatHorizontal = 1e-4F;
    }

    SlopeInfo AnalyzeSlope(Vec3 surfaceNormal, float yawRadians) noexcept
    {
        SlopeInfo result{};
        const float length = Length(surfaceNormal);
        if (!std::isfinite(length) || length < 0.5F || !std::isfinite(yawRadians)) {
            return result;
        }
        const Vec3 n{ surfaceNormal.x / length, surfaceNormal.y / length, surfaceNormal.z / length };
        if (n.z <= 0.0F) {
            return result;
        }

        result.valid = true;
        result.angleDegrees = std::acos(std::clamp(n.z, -1.0F, 1.0F)) * kDegrees;
        result.gravityAlongSlope = HorizontalLength(n);

        // Gravity projected onto the surface points along the normal's horizontal component.
        const float horizontal = HorizontalLength(n);
        if (horizontal < kFlatHorizontal) {
            return result;
        }
        result.downhillX = n.x / horizontal;
        result.downhillY = n.y / horizontal;

        const float forwardX = std::sin(yawRadians);
        const float forwardY = std::cos(yawRadians);
        const float ahead = result.downhillX * forwardX + result.downhillY * forwardY;
        const float right = result.downhillX * forwardY - result.downhillY * forwardX;
        result.fallLineOffsetDegrees = std::atan2(right, ahead) * kDegrees;
        return result;
    }
}
