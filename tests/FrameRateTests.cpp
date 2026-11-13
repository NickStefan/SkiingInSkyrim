#include "Check.h"

#include "core/Impact.h"
#include "core/SkiPhysics.h"

#include <array>
#include <cmath>
#include <numbers>
#include <vector>

using namespace SkiingInSkyrim::Core;

namespace
{
    constexpr float kRadians = std::numbers::pi_v<float> / 180.0F;

    struct Checkpoint
    {
        float speed;
        float yawDegrees;
        float x;
        float y;
    };

    // A scripted run down a 25 degree slope falling along +y, the same whatever the frame rate: a straight
    // run, a right carve, a left carve, a skid, then steering with the stick. One checkpoint a second.
    std::vector<Checkpoint> ScriptedRun(int fps)
    {
        constexpr int kSeconds = 12;
        const float   dt = 1.0F / static_cast<float>(fps);
        const float   slope = 25.0F * kRadians;
        SkiState      state;
        float         yaw = 0.0F;
        std::vector<Checkpoint> checkpoints;
        for (int frame = 0; frame < kSeconds * fps; ++frame) {
            const int second = frame / fps;
            SkiInput  input;
            input.deltaSeconds = dt;
            input.grounded = true;
            input.hasNormal = true;
            input.normal = { 0.0F, std::sin(slope), std::cos(slope) };
            input.yaw = yaw;
            if (second >= 3 && second < 6) {
                input.edgeTarget = 0.6F;
            } else if (second >= 6 && second < 9) {
                input.edgeTarget = -0.6F;
            } else if (second == 9) {
                input.skidHeld = true;
            } else if (second >= 10) {
                input.aimYaw = 30.0F * kRadians;
                input.aimStrength = 0.7F;
                input.pushStrength = 0.7F;
            }
            const auto step = StepSki(state, input);
            yaw = step.yaw;
            if ((frame + 1) % fps == 0) {
                checkpoints.push_back({ Length(step.velocity), yaw / kRadians, step.velocity.x, step.velocity.y });
            }
        }
        return checkpoints;
    }

    struct Crash
    {
        int   impacts{ 0 };
        float lost{ 0.0F };  // m/s, the biggest impact
        int   frames{ 0 };
    };

    // Skiing at speed, then stopped by something over stopSeconds (0: within one frame).
    Crash SimulatedCrash(int fps, float speed, float stopSeconds)
    {
        const float   dt = 1.0F / static_cast<float>(fps);
        ImpactTracker tracker;
        Crash         crash;
        const auto    record = [&](const std::optional<Impact>& impact) {
            if (impact) {
                ++crash.impacts;
                if (Length(impact->lost) > crash.lost) {
                    crash.lost = Length(impact->lost);
                    crash.frames = impact->frames;
                }
            }
        };
        for (int frame = 0; frame < fps; ++frame) {
            record(tracker.Update({ { 0.0F, speed, 0.0F }, false, dt }));
        }
        float now = speed;
        while (now > 0.0F) {
            now = stopSeconds > 0.0F ? std::max(0.0F, now - speed * dt / stopSeconds) : 0.0F;
            record(tracker.Update({ { 0.0F, now, 0.0F }, false, dt }));
        }
        for (int frame = 0; frame < fps; ++frame) {
            record(tracker.Update({ {}, false, dt }));
        }
        return crash;
    }
}

// What the model did at 60 fps when this was recorded. If a change moves these, it changes how skiing feels at
// 60 fps: deliberate changes update them, nothing else should.
TEST_CASE("at 60 fps the scripted run comes out as recorded")
{
    constexpr std::array<Checkpoint, 12> kRecorded{ {
        { 3.679884F, 0.000000F, 0.000000F, 3.679884F },
        { 7.149358F, 0.000000F, 0.000000F, 7.149358F },
        { 9.941114F, 0.000000F, 0.000000F, 9.941114F },
        { 11.267877F, 35.176159F, 5.498704F, 9.835105F },
        { 10.391710F, 71.389954F, 7.836009F, 6.825292F },
        { 7.526766F, 103.885315F, 6.571949F, 3.669017F },
        { 5.460845F, 84.215645F, 5.285541F, 1.372546F },
        { 6.204409F, 60.067947F, 5.376851F, 3.095830F },
        { 8.645048F, 29.005051F, 4.191869F, 7.560760F },
        { 1.343163F, 115.000183F, 0.567649F, 1.217317F },
        { 4.206841F, 30.000002F, 1.645225F, 3.871789F },
        { 7.136670F, 30.000002F, 3.185272F, 6.386400F },
    } };
    const auto run = ScriptedRun(60);
    CHECK(run.size() == kRecorded.size());
    for (std::size_t i = 0; i < run.size() && i < kRecorded.size(); ++i) {
        CHECK_NEAR(run[i].speed, kRecorded[i].speed, 1e-3F);
        CHECK_NEAR(run[i].yawDegrees, kRecorded[i].yawDegrees, 0.01F);
        CHECK_NEAR(run[i].x, kRecorded[i].x, 1e-3F);
        CHECK_NEAR(run[i].y, kRecorded[i].y, 1e-3F);
    }
}

// Running straight and carving come out the same at any frame rate. Changing edges and coming out of a skid
// land on slightly different frames, so later on the run drifts a little: a few degrees, a few percent.
TEST_CASE("the scripted run is nearly the same at 20 to 144 fps as at 60")
{
    const auto reference = ScriptedRun(60);
    for (const int fps : { 20, 30, 40, 45, 50, 90, 120, 144 }) {
        const auto run = ScriptedRun(fps);
        CHECK(run.size() == reference.size());
        for (std::size_t i = 0; i < 6 && i < run.size(); ++i) {
            CHECK(std::fabs(run[i].speed - reference[i].speed) <= 0.03F * reference[i].speed);
            CHECK_NEAR(run[i].yawDegrees, reference[i].yawDegrees, 1.0F);
        }
        for (std::size_t i = 6; i < 9 && i < run.size(); ++i) {
            CHECK(std::fabs(run[i].speed - reference[i].speed) <= 0.06F * reference[i].speed);
            CHECK_NEAR(run[i].yawDegrees, reference[i].yawDegrees, 4.0F);
        }
    }
}

TEST_CASE("an instant stop is one impact of the whole speed at any frame rate")
{
    for (const int fps : { 20, 30, 45, 60, 90, 120, 144 }) {
        const auto crash = SimulatedCrash(fps, 15.0F, 0.0F);
        CHECK(crash.impacts == 1);
        CHECK_NEAR(crash.lost, 15.0F, 1e-3F);
        CHECK(crash.frames == 1);
    }
}

// The impact thresholds are per frame, so a stop spread over time counts only below some frame rate. Recorded
// so a change to that is noticed; not how it should be.
TEST_CASE("a stop spread over 50 ms counts as an impact up to 90 fps, not at 120")
{
    for (const int fps : { 20, 30, 45, 60, 90 }) {
        CHECK(SimulatedCrash(fps, 15.0F, 0.05F).impacts == 1);
    }
    for (const int fps : { 120, 144 }) {
        CHECK(SimulatedCrash(fps, 15.0F, 0.05F).impacts == 0);
    }
}
