#include "Check.h"

#include "core/SkiPose.h"

#include <numbers>

using namespace SkiingInSkyrim::Core;

namespace
{
    constexpr float kDegrees = std::numbers::pi_v<float> / 180.0F;

    Vec3 Cross(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }

    // The axes are unit length, at right angles and right-handed (right x forward = up).
    void CheckAxes(const SkiPose& pose)
    {
        CHECK_NEAR(Length(pose.right), 1.0F, 1e-5F);
        CHECK_NEAR(Length(pose.forward), 1.0F, 1e-5F);
        CHECK_NEAR(Length(pose.up), 1.0F, 1e-5F);
        CHECK_NEAR(Dot(pose.right, pose.forward), 0.0F, 1e-5F);
        CHECK_NEAR(Dot(pose.right, pose.up), 0.0F, 1e-5F);
        CHECK_NEAR(Dot(pose.forward, pose.up), 0.0F, 1e-5F);
        const auto up = Cross(pose.right, pose.forward);
        CHECK_NEAR(up.x, pose.up.x, 1e-5F);
        CHECK_NEAR(up.y, pose.up.y, 1e-5F);
        CHECK_NEAR(up.z, pose.up.z, 1e-5F);
    }
}

TEST_CASE("a flat ski on level ground lies straight under its foot")
{
    const auto pose = PlaceSki({ 0.0F, 0.0F, 1.0F }, 0.0F, 0.0F, -8.0F, 5.0F);
    CheckAxes(pose);
    CHECK_NEAR(pose.forward.y, 1.0F, 1e-6F);
    CHECK_NEAR(pose.up.z, 1.0F, 1e-6F);
    CHECK_NEAR(pose.position.x, -8.0F, 1e-6F);
    CHECK_NEAR(pose.position.y, 0.0F, 1e-6F);
    CHECK_NEAR(pose.position.z, 0.0F, 1e-6F);
}

TEST_CASE("a ski pointing down a slope tips down with it")
{
    // Downhill is +Y: the normal leans that way.
    const Vec3 normal{ 0.0F, std::sin(30.0F * kDegrees), std::cos(30.0F * kDegrees) };
    const auto pose = PlaceSki(normal, 0.0F, 0.0F, 0.0F, 5.0F);
    CheckAxes(pose);
    CHECK_NEAR(std::asin(-pose.forward.z) / kDegrees, 30.0F, 1e-3F);
    CHECK_NEAR(Dot(pose.forward, normal), 0.0F, 1e-6F);
}

TEST_CASE("across a slope the downhill ski sits lower")
{
    // Downhill is to the character's right (+X).
    const Vec3 normal{ std::sin(20.0F * kDegrees), 0.0F, std::cos(20.0F * kDegrees) };
    const auto left = PlaceSki(normal, 0.0F, 0.0F, -8.0F, 5.0F);
    const auto right = PlaceSki(normal, 0.0F, 0.0F, 8.0F, 5.0F);
    CHECK(right.position.z < 0.0F);
    CHECK(left.position.z > 0.0F);
    CHECK_NEAR(right.position.z, -8.0F * std::tan(20.0F * kDegrees), 1e-4F);
}

TEST_CASE("the right edge rolls the ski onto its right side, lifted onto that edge")
{
    const float edge = 40.0F * kDegrees;
    const auto pose = PlaceSki({ 0.0F, 0.0F, 1.0F }, 0.0F, edge, 0.0F, 5.0F);
    CheckAxes(pose);
    CHECK(pose.up.x > 0.0F);
    CHECK(pose.right.z < 0.0F);
    // The right edge, halfWidth along the ski's right axis, touches the ground.
    CHECK_NEAR(pose.position.z + pose.right.z * 5.0F, 0.0F, 1e-4F);
    const auto left = PlaceSki({ 0.0F, 0.0F, 1.0F }, 0.0F, -edge, 0.0F, 5.0F);
    CHECK(left.up.x < 0.0F);
    CHECK_NEAR(left.position.z - left.right.z * 5.0F, 0.0F, 1e-4F);
}

TEST_CASE("a twisted ski still lies on the slope")
{
    const Vec3 normal{ 0.0F, std::sin(25.0F * kDegrees), std::cos(25.0F * kDegrees) };
    const auto pose = PlaceSki(normal, 60.0F * kDegrees, 15.0F * kDegrees, 4.0F, 5.0F);
    CheckAxes(pose);
    CHECK_NEAR(Dot(pose.forward, normal), 0.0F, 1e-5F);
    CHECK(pose.forward.x > 0.5F);
}
