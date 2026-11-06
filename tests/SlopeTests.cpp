#include "Check.h"

#include "core/Slope.h"

#include <limits>
#include <numbers>

using SkiingInSkyrim::Core::AnalyzeSlope;
using SkiingInSkyrim::Core::Vec3;

namespace
{
    constexpr float kPi = std::numbers::pi_v<float>;

    Vec3 NormalFor(float slopeDegrees, float downhillX, float downhillY)
    {
        const float radians = slopeDegrees * kPi / 180.0F;
        return { std::sin(radians) * downhillX, std::sin(radians) * downhillY, std::cos(radians) };
    }
}

TEST_CASE("flat ground has no downhill direction")
{
    const auto slope = AnalyzeSlope({ 0.0F, 0.0F, 1.0F }, 0.0F);
    CHECK(slope.valid);
    CHECK_NEAR(slope.angleDegrees, 0.0F, 1e-4F);
    CHECK_NEAR(slope.gravityAlongSlope, 0.0F, 1e-4F);
    CHECK_NEAR(slope.downhillX, 0.0F, 1e-6F);
    CHECK_NEAR(slope.downhillY, 0.0F, 1e-6F);
}

TEST_CASE("slope angle and gravity fraction follow the normal")
{
    const auto slope = AnalyzeSlope(NormalFor(30.0F, 1.0F, 0.0F), 0.0F);
    CHECK(slope.valid);
    CHECK_NEAR(slope.angleDegrees, 30.0F, 1e-3F);
    CHECK_NEAR(slope.gravityAlongSlope, 0.5F, 1e-4F);
    CHECK_NEAR(slope.downhillX, 1.0F, 1e-5F);
    CHECK_NEAR(slope.downhillY, 0.0F, 1e-5F);
}

TEST_CASE("unnormalized normals give the same result")
{
    const auto slope = AnalyzeSlope({ 0.0F, 5.0F, 5.0F }, 0.0F);
    CHECK(slope.valid);
    CHECK_NEAR(slope.angleDegrees, 45.0F, 1e-3F);
    CHECK_NEAR(slope.downhillY, 1.0F, 1e-5F);
}

TEST_CASE("fall-line offset is signed relative to facing")
{
    // Downhill is +X (east). Facing north (yaw 0), east is to the right.
    CHECK_NEAR(AnalyzeSlope(NormalFor(20.0F, 1.0F, 0.0F), 0.0F).fallLineOffsetDegrees, 90.0F, 1e-3F);
    // Facing east (yaw +90 deg) looks straight down the fall line.
    CHECK_NEAR(AnalyzeSlope(NormalFor(20.0F, 1.0F, 0.0F), kPi / 2.0F).fallLineOffsetDegrees, 0.0F, 1e-3F);
    // Facing south, east is to the left.
    CHECK_NEAR(AnalyzeSlope(NormalFor(20.0F, 1.0F, 0.0F), kPi).fallLineOffsetDegrees, -90.0F, 1e-3F);
    // Facing west, downhill is directly behind.
    CHECK_NEAR(std::fabs(AnalyzeSlope(NormalFor(20.0F, 1.0F, 0.0F), -kPi / 2.0F).fallLineOffsetDegrees), 180.0F, 1e-3F);
}

TEST_CASE("degenerate inputs are rejected")
{
    CHECK(!AnalyzeSlope({ 0.0F, 0.0F, 0.0F }, 0.0F).valid);
    CHECK(!AnalyzeSlope({ 0.0F, 0.0F, -1.0F }, 0.0F).valid);
    CHECK(!AnalyzeSlope({ 0.0F, 0.0F, 1.0F }, std::numeric_limits<float>::quiet_NaN()).valid);
    CHECK(!AnalyzeSlope({ std::numeric_limits<float>::infinity(), 0.0F, 1.0F }, 0.0F).valid);
}
