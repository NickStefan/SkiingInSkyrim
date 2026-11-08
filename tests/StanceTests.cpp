#include "Check.h"

#include "core/Stance.h"

#include <numbers>

using namespace SkiingInSkyrim::Core;

namespace
{
    constexpr float kDegrees = std::numbers::pi_v<float> / 180.0F;
}

TEST_CASE("standing straight leaves the hips where they are")
{
    const auto shift = HipShiftFor(30.0F, 28.0F, 0.0F, 0.0F);
    CHECK_NEAR(shift.forward, 0.0F, 1e-5F);
    CHECK_NEAR(shift.up, 0.0F, 1e-5F);
}

TEST_CASE("bending the knees lowers the hips by what the legs lose in height")
{
    const float thigh = 30.0F;
    const float shin = 28.0F;
    const auto  shift = HipShiftFor(thigh, shin, 40.0F * kDegrees, 25.0F * kDegrees);
    const float expectedDrop = thigh * (1.0F - std::cos(40.0F * kDegrees)) + shin * (1.0F - std::cos(25.0F * kDegrees));
    CHECK_NEAR(shift.up, -expectedDrop, 1e-4F);
    // Thighs lean further than the shins: the knees are ahead, so the hips go back to stay over the feet.
    CHECK(shift.forward < 0.0F);
}

TEST_CASE("equal legs leaning equally keep the hips over the ankles")
{
    const auto shift = HipShiftFor(30.0F, 30.0F, 25.0F * kDegrees, 25.0F * kDegrees);
    CHECK_NEAR(shift.forward, 0.0F, 1e-4F);
    CHECK(shift.up < 0.0F);
}

TEST_CASE("a tuck bends further than cruising")
{
    const StanceSettings settings;
    const auto cruise = Stance(settings, 0.0F);
    const auto tucked = Stance(settings, 1.0F);
    CHECK(tucked.hip > cruise.hip);
    CHECK(tucked.ankle > cruise.ankle);
    CHECK(tucked.torso > cruise.torso);
    CHECK_NEAR(cruise.hip, settings.hipFlexDegrees * kDegrees, 1e-6F);
    CHECK_NEAR(Stance(settings, 2.0F).hip, tucked.hip, 1e-6F);
}

TEST_CASE("a turn leans the body in as far as its pull calls for, the upper body less")
{
    StanceSettings settings;
    settings.turnLean = 1.0F;
    settings.hipAngulation = 0.0F;
    settings.upperBodyUpright = 0.4F;
    const float g = 9.81F;
    const auto  right = LeanFor(settings, g * std::tan(30.0F * kDegrees), g, 30.0F * kDegrees);
    CHECK_NEAR(right.hips, 30.0F * kDegrees, 1e-4F);
    CHECK_NEAR(right.upper, 18.0F * kDegrees, 1e-4F);
    const auto left = LeanFor(settings, -g * std::tan(30.0F * kDegrees), g, -30.0F * kDegrees);
    CHECK_NEAR(left.hips, -right.hips, 1e-5F);

    const auto wild = LeanFor(settings, 20.0F * g, g, 0.0F);
    CHECK(wild.hips < 60.0F * kDegrees);
}

TEST_CASE("an edge held without turning pushes the hips in and leaves the upper body upright")
{
    StanceSettings settings;
    settings.hipAngulation = 0.5F;
    const auto held = LeanFor(settings, 0.0F, 9.81F, -40.0F * kDegrees);
    CHECK_NEAR(held.hips, -20.0F * kDegrees, 1e-4F);
    CHECK_NEAR(held.upper, 0.0F, 1e-6F);
}

TEST_CASE("a leg reaches a target in range, bones at their lengths, the knee bending forward")
{
    const Vec3 hip{ 10.0F, 0.0F, 50.0F };
    const Vec3 target{ 8.0F, 5.0F, 0.0F };
    const auto leg = SolveLeg(hip, target, 30.0F, 28.0F, { 0.0F, 1.0F, 0.0F });
    auto distance = [](Vec3 a, Vec3 b) { return Length({ a.x - b.x, a.y - b.y, a.z - b.z }); };
    CHECK_NEAR(distance(leg.ankle, target), 0.0F, 1e-3F);
    CHECK_NEAR(distance(hip, leg.knee), 30.0F, 1e-3F);
    CHECK_NEAR(distance(leg.knee, leg.ankle), 28.0F, 1e-3F);
    CHECK(leg.knee.y > 2.5F);  // ahead of the line from hip to ankle
    CHECK_NEAR(leg.excess, 0.0F, 1e-6F);
    CHECK_NEAR(Length(leg.hinge), 1.0F, 1e-4F);
    CHECK(leg.hinge.x > 0.9F);
}

TEST_CASE("solving the leg for the shifted hips gives back the stance's angles")
{
    const float thigh = 30.0F;
    const float shin = 28.0F;
    const float hipAngle = 40.0F * kDegrees;
    const float ankleAngle = 25.0F * kDegrees;
    const auto  shift = HipShiftFor(thigh, shin, hipAngle, ankleAngle);
    const auto  leg = SolveLeg({ 0.0F, shift.forward, thigh + shin + shift.up }, { 0.0F, 0.0F, 0.0F }, thigh, shin,
         { 0.0F, 1.0F, 0.0F });
    CHECK_NEAR(leg.knee.y - shift.forward, thigh * std::sin(hipAngle), 1e-3F);
    CHECK_NEAR(thigh + shin + shift.up - leg.knee.z, thigh * std::cos(hipAngle), 1e-3F);
}

TEST_CASE("a skating stride pushes its ski out, lifts it back, and moves the weight across")
{
    const StanceSettings settings;
    const float push = 0.35F;
    CHECK(!SkateAt(settings, 0.0F, 0.1F, push).active);
    CHECK(!SkateAt(settings, 1.0F, -1.0F, push).active);
    CHECK(!SkateAt(settings, 1.0F, 5.0F, push).active);

    const auto start = SkateAt(settings, 1.0F, 0.0F, push);
    CHECK(start.active);
    CHECK_NEAR(start.legs[1].out, 0.0F, 1e-5F);
    CHECK_NEAR(start.hipsOver, 1.0F, 1e-5F);  // still over the right ski, the last stride's glide

    const auto pushed = SkateAt(settings, 1.0F, push, push);
    CHECK_NEAR(pushed.legs[1].out, settings.skatePushOut, 1e-3F);
    CHECK_NEAR(pushed.legs[1].lift, 0.0F, 1e-5F);
    CHECK(pushed.legs[1].edge > 0.0F);
    CHECK_NEAR(pushed.legs[0].out, 0.0F, 1e-6F);  // the gliding ski stays under its hip
    CHECK_NEAR(pushed.hipsOver, -1.0F, 1e-5F);    // now over the left
    CHECK_NEAR(pushed.armSwing, 1.0F, 1e-5F);     // right arm back

    const auto recovering = SkateAt(settings, 1.0F, push + 0.15F, push);
    CHECK(recovering.legs[1].lift > 0.9F * settings.skateLift);
    CHECK(recovering.legs[1].out < settings.skatePushOut);

    const auto gliding = SkateAt(settings, 1.0F, push + 0.5F, push);
    CHECK(gliding.active);
    CHECK_NEAR(gliding.legs[1].out, 0.0F, 1e-5F);
    CHECK_NEAR(gliding.legs[1].lift, 0.0F, 1e-5F);

    // The left stride mirrors it, and starts where the right one left the weight.
    const auto left = SkateAt(settings, -1.0F, 0.0F, push);
    CHECK_NEAR(left.hipsOver, -1.0F, 1e-5F);
    CHECK_NEAR(left.armSwing, 1.0F, 1e-5F);
}

TEST_CASE("a target out of reach straightens the leg toward it and says how far short")
{
    const auto leg = SolveLeg({ 0.0F, 0.0F, 100.0F }, { 0.0F, 0.0F, 0.0F }, 30.0F, 28.0F, { 0.0F, 1.0F, 0.0F });
    CHECK_NEAR(leg.excess, 100.0F - 58.0F * 0.999F, 1e-3F);
    CHECK(leg.ankle.z > 41.0F);
}
