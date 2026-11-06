#include "Check.h"

#include "core/GroundEstimate.h"
#include "core/Slope.h"

using namespace SkiingInSkyrim::Core;

namespace
{
    constexpr float kFrame = 1.0F / 60.0F;
    constexpr float kUnitsPerMeter = 70.0F;

    // Moves along +Y at the given speed, recording position so smoothing sees real distance.
    struct Walker
    {
        GroundTracker tracker;
        Vec3          position{};

        const GroundEstimate& Step(bool supported, Vec3 support, Vec3 surface, Vec3 velocity, float dt = kFrame)
        {
            position.x += velocity.x * kUnitsPerMeter * dt;
            position.y += velocity.y * kUnitsPerMeter * dt;
            position.z += velocity.z * kUnitsPerMeter * dt;
            return tracker.Update({ supported, surface, support, position, velocity, dt });
        }

        const GroundEstimate& Ground(Vec3 normal, Vec3 velocity) { return Step(true, normal, normal, velocity); }
        const GroundEstimate& Unsupported(Vec3 velocity) { return Step(false, {}, {}, velocity); }
    };

    float SlopeOf(const GroundEstimate& estimate) { return AnalyzeSlope(estimate.normal, 0.0F).angleDegrees; }

    // Terrain facets logged at 17:00:49-50 on the first hill run.
    constexpr Vec3 kFacetA{ 0.058F, 0.351F, 0.935F };  // 20.8 deg
    constexpr Vec3 kFacetB{ 0.060F, 0.298F, 0.953F };  // 17.7 deg
    constexpr Vec3 kRunning{ 0.8F, 4.8F, -1.7F };
}

TEST_CASE("first valid normal is used directly")
{
    Walker walker;
    const auto& estimate = walker.Ground(kFacetA, kRunning);
    CHECK(estimate.hasNormal);
    CHECK(estimate.source == NormalSource::Support);
    CHECK_NEAR(SlopeOf(estimate), 20.8F, 0.1F);
}

TEST_CASE("terrain facet noise is smoothed")
{
    Walker walker;
    walker.Ground(kFacetA, kRunning);
    float lowest = 90.0F;
    float highest = 0.0F;
    // Alternate facets every 15 frames (~1.2 m at running speed) for 3 seconds.
    for (int frame = 0; frame < 180; ++frame) {
        const auto& estimate = walker.Ground((frame / 15) % 2 == 0 ? kFacetB : kFacetA, kRunning);
        if (frame >= 60) {
            lowest = std::min(lowest, SlopeOf(estimate));
            highest = std::max(highest, SlopeOf(estimate));
        }
    }
    CHECK(highest - lowest < 2.0F);  // raw input swings 3.1 deg
    CHECK(lowest > 17.7F);
    CHECK(highest < 20.8F);
}

TEST_CASE("smoothing follows distance, not time")
{
    const auto slopeAfter = [](float speed, float seconds) {
        Walker walker;
        walker.Ground({ 0.0F, 0.0F, 1.0F }, { 0.0F, speed, 0.0F });
        GroundEstimate last;
        for (float t = 0.0F; t < seconds; t += kFrame) {
            last = walker.Ground(kFacetA, { 0.0F, speed, 0.0F });
        }
        return SlopeOf(last);
    };
    // Both cover ~1 m of new slope.
    const float slow = slopeAfter(2.0F, 0.5F);
    const float fast = slopeAfter(10.0F, 0.1F);
    CHECK(slow > 5.0F);
    CHECK(fast > 5.0F);
    // The stationary drift term gives the slow run a little extra.
    CHECK(std::fabs(slow - fast) < 3.0F);
}

TEST_CASE("brief support flicker stays grounded")
{
    // Stairs at 17:00:57: unsupported for 1-6 frames at a time.
    Walker walker;
    walker.Ground(kFacetA, kRunning);
    for (int frame = 0; frame < 6; ++frame) {
        CHECK(walker.Unsupported({ -3.3F, 4.4F, -0.65F }).contact == Contact::Grounded);
    }
    const auto& back = walker.Ground(kFacetA, kRunning);
    CHECK(back.contact == Contact::Grounded);
    CHECK(back.unsupportedSeconds == 0.0F);
}

TEST_CASE("running hops down a steep slope stay grounded")
{
    // 18:35:46.78-47.11: unsupported ~0.25 s at 5.5 m/s on ~45 deg, descending no faster than the slope.
    Walker walker;
    const Vec3 steep{ -0.493F, 0.493F, 0.717F };
    walker.Ground(steep, { -2.9F, 4.7F, -3.9F });
    GroundEstimate estimate;
    for (int frame = 0; frame < 15; ++frame) {
        estimate = walker.Unsupported({ -2.9F, 4.7F, -3.9F });
    }
    CHECK(estimate.contact == Contact::Grounded);
}

TEST_CASE("zero normals while unsupported keep the last ground normal")
{
    Walker walker;
    walker.Ground(kFacetA, kRunning);
    const auto& estimate = walker.Unsupported(kRunning);
    CHECK(estimate.hasNormal);
    CHECK(estimate.source == NormalSource::None);
    CHECK_NEAR(SlopeOf(estimate), 20.8F, 0.1F);
}

TEST_CASE("long loss of support becomes airborne")
{
    Walker walker;
    walker.Ground(kFacetA, kRunning);
    GroundEstimate estimate;
    for (int frame = 0; frame < 20; ++frame) {
        estimate = walker.Unsupported(kRunning);
    }
    CHECK(estimate.contact == Contact::Airborne);
    CHECK(walker.Ground(kFacetA, kRunning).contact == Contact::Grounded);
}

TEST_CASE("falling much faster than the slope explains is airborne at once")
{
    // Ledge drop at 18:17:37.9: -7.7 m/s down on ~45 deg ground at 5.5 m/s.
    Walker walker;
    const Vec3 steep{ -0.482F, 0.526F, 0.701F };
    walker.Ground(steep, { -2.8F, 4.2F, -5.1F });
    CHECK(walker.Unsupported({ -2.8F, 4.7F, -12.0F }).contact == Contact::Airborne);
}

TEST_CASE("descending a steep slope while flickering is not a fall")
{
    // 17:00:58: brief unsupported frames while running down a 42 deg step face.
    Walker walker;
    const Vec3 steep{ -0.155F, -0.656F, 0.739F };
    walker.Ground(steep, { 0.0F, -3.5F, -3.0F });
    CHECK(walker.Unsupported({ 0.0F, -3.5F, -3.0F }).contact == Contact::Grounded);
}

TEST_CASE("rock contact uses the support normal and flags a wall")
{
    // 18:19:20: surface 79.5 deg (rock), support 21.6 deg (ground).
    Walker walker;
    const auto& estimate = walker.Step(true, { -0.349F, -0.116F, 0.930F }, { -0.940F, -0.288F, 0.182F }, { 0.4F, -1.2F, 0.1F });
    CHECK(estimate.wallContact);
    CHECK(estimate.source == NormalSource::Support);
    CHECK_NEAR(SlopeOf(estimate), 21.6F, 0.2F);
}

TEST_CASE("wall-only contact does not change the slope")
{
    Walker walker;
    walker.Ground(kFacetA, kRunning);
    GroundEstimate estimate;
    for (int frame = 0; frame < 30; ++frame) {
        estimate = walker.Step(true, { -0.927F, -0.292F, 0.236F }, { -0.984F, 0.0F, 0.176F }, { 0.0F, 0.2F, 0.0F });
    }
    CHECK(estimate.wallContact);
    CHECK(estimate.source == NormalSource::None);
    CHECK_NEAR(SlopeOf(estimate), 20.8F, 0.1F);
}

TEST_CASE("teleport resets the estimate")
{
    Walker walker;
    walker.Ground(kFacetA, kRunning);
    walker.position.x += 10000.0F;
    const auto& estimate = walker.Ground({ 0.0F, 0.0F, 1.0F }, {});
    CHECK_NEAR(SlopeOf(estimate), 0.0F, 0.01F);
}
