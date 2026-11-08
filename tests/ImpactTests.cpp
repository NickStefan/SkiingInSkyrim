#include "Check.h"

#include "core/Impact.h"

#include <optional>

using namespace SkiingInSkyrim::Core;

namespace
{
    constexpr float kFrame = 1.0F / 60.0F;

    // Feeds the frames in order; returns the impact reported, if any.
    std::optional<Impact> Run(ImpactTracker& tracker, std::initializer_list<ImpactSample> frames)
    {
        std::optional<Impact> found;
        for (const auto& frame : frames) {
            if (auto impact = tracker.Update(frame)) {
                found = impact;
            }
        }
        return found;
    }
}

TEST_CASE("falling freely is not an impact")
{
    ImpactTracker tracker;
    std::optional<Impact> found;
    Vec3                  velocity{ 5.0F, 0.0F, 0.0F };
    for (int i = 0; i < 120; ++i) {
        velocity.z -= 9.81F * kFrame;
        if (auto impact = tracker.Update({ velocity, true, kFrame })) {
            found = impact;
        }
    }
    CHECK(!found);
}

TEST_CASE("slamming onto flat ground loses the downward speed")
{
    ImpactTracker tracker;
    const auto    impact = Run(tracker, {
        { { 5.0F, 0.0F, -15.0F }, true, kFrame },
        { { 5.0F, 0.0F, 0.0F }, false, kFrame },
        { { 5.0F, 0.0F, 0.0F }, false, kFrame },
    });
    CHECK(impact.has_value());
    if (impact) {
        CHECK_NEAR(impact->lost.z, -15.0F - 9.81F * kFrame, 1e-3F);
        CHECK_NEAR(impact->lost.x, 0.0F, 1e-4F);
        CHECK(impact->frames == 1);
    }
}

TEST_CASE("landing along a slope loses little")
{
    ImpactTracker tracker;
    // Falling at 40 degrees below horizontal onto a 40 degree slope: nothing goes into it.
    const auto impact = Run(tracker, {
        { { 0.0F, 15.0F, -12.6F }, true, kFrame },
        { { 0.0F, 15.0F, -12.6F }, false, kFrame },
        { { 0.0F, 15.0F, -12.6F }, false, kFrame },
    });
    CHECK(!impact);
}

TEST_CASE("hitting a tree loses the forward speed, spread over a few frames")
{
    ImpactTracker tracker;
    const auto    impact = Run(tracker, {
        { { 0.0F, 12.0F, 0.0F }, false, kFrame },
        { { 0.0F, 5.0F, 0.0F }, false, kFrame },
        { { 0.0F, 1.0F, 0.0F }, false, kFrame },
        { { 0.0F, 1.0F, 0.0F }, false, kFrame },
    });
    CHECK(impact.has_value());
    if (impact) {
        CHECK_NEAR(impact->lost.y, 11.0F, 1e-3F);
        CHECK(impact->frames == 2);
        CHECK_NEAR(impact->before.y, 12.0F, 1e-4F);
    }
}

TEST_CASE("gaining speed is never an impact")
{
    ImpactTracker tracker;
    const auto    impact = Run(tracker, {
        { { 0.0F, 5.0F, 0.0F }, false, kFrame },
        { { 0.0F, 5.0F, 6.0F }, true, kFrame },  // a jump
        { { 0.0F, 15.0F, 6.0F }, true, kFrame },  // a push
        { { 0.0F, 15.0F, 6.0F }, true, kFrame },
    });
    CHECK(!impact);
}

TEST_CASE("speed we set ourselves, and a reset, are not impacts")
{
    ImpactTracker tracker;
    tracker.Update({ { 0.0F, 10.0F, -8.0F }, true, kFrame });
    tracker.Override({ 0.0F, 10.0F, 0.0F });
    CHECK(!tracker.Update({ { 0.0F, 10.0F, 0.0F }, false, kFrame }));

    tracker.Update({ { 0.0F, 20.0F, 0.0F }, false, kFrame });
    tracker.Reset();
    CHECK(!Run(tracker, { { { 0.0F, 0.0F, 0.0F }, false, kFrame }, { { 0.0F, 0.0F, 0.0F }, false, kFrame } }));
}

TEST_CASE("equivalent fall height matches free fall")
{
    CHECK_NEAR(EquivalentFallMetres(14.0F, 9.81F), 14.0F * 14.0F / (2.0F * 9.81F), 1e-4F);
    CHECK(EquivalentFallMetres(5.0F, 0.0F) == 0.0F);
}
