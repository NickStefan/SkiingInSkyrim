#include "Check.h"

#include "core/CameraFollow.h"

#include <numbers>

using namespace SkiingInSkyrim::Core;

namespace
{
    constexpr float kFrame = 1.0F / 60.0F;
    constexpr float kPi = std::numbers::pi_v<float>;
    constexpr float kDegrees = kPi / 180.0F;

    float AngleBetween(float a, float b) { return std::fabs(std::remainder(a - b, 2.0F * kPi)); }
    Vec2  Moving(float heading, float speed) { return { std::sin(heading) * speed, std::cos(heading) * speed }; }

    struct Chase
    {
        CameraFollowState    state;
        CameraFollowSettings settings;
        float                camera{ 0.0F };

        void Step(float skiYaw, Vec2 velocity, float look = 0.0F, std::optional<float> heading = std::nullopt)
        {
            camera = UpdateCameraFollow(state, settings, kFrame, camera + look, skiYaw, velocity, heading);
        }

        // look: the player's camera input each frame, applied before our update as the game does.
        void Run(float seconds, float skiYaw, Vec2 velocity, float look = 0.0F,
            std::optional<float> heading = std::nullopt)
        {
            for (int i = 0; i < static_cast<int>(seconds / kFrame + 0.5F); ++i) {
                Step(skiYaw, velocity, look, heading);
            }
        }
    };
}

TEST_CASE("the chase camera starts where the camera already is")
{
    Chase chase;
    chase.camera = 1.0F;
    chase.Run(kFrame, 0.0F, { 0.0F, 10.0F });
    CHECK(AngleBetween(chase.camera, 1.0F) < 0.06F);
}

TEST_CASE("the camera swings round to the direction of travel, not the skis")
{
    Chase chase;
    chase.Run(kFrame, 0.0F, { 0.0F, 10.0F });
    chase.Run(6.0F, kPi / 2.0F, { 10.0F, 0.0F });  // travelling +X, skis also +X
    CHECK(AngleBetween(chase.camera, kPi / 2.0F) < 0.02F);

    chase.Run(6.0F, kPi, { 10.0F, 0.0F });  // skis turned 90 degrees, still travelling +X
    CHECK(AngleBetween(chase.camera, kPi / 2.0F) < 0.02F);
}

TEST_CASE("the follow lags rather than snapping")
{
    Chase chase;
    chase.Run(kFrame, 0.0F, { 0.0F, 10.0F });
    chase.Run(0.1F, kPi / 2.0F, { 10.0F, 0.0F });
    CHECK(chase.camera > 0.02F && chase.camera < 0.6F);
}

TEST_CASE("quick back-and-forth carves barely move the camera")
{
    Chase chase;
    chase.Run(3.0F, 0.0F, { 0.0F, 12.0F });
    float lowest = 0.0F;
    float highest = 0.0F;
    for (int i = 0; i < 6 * 60; ++i) {
        // Heading swings 30 degrees either side every two seconds, as in short-radius turns.
        const float heading = 30.0F * kDegrees * std::sin(2.0F * kPi * 0.5F * static_cast<float>(i) * kFrame);
        chase.Step(heading, Moving(heading, 12.0F));
        const float camera = std::remainder(chase.camera, 2.0F * kPi);
        lowest = std::min(lowest, camera);
        highest = std::max(highest, camera);
    }
    CHECK(highest - lowest < 16.0F * kDegrees);
}

TEST_CASE("a sustained turn is followed")
{
    Chase chase;
    chase.Run(3.0F, 0.0F, { 0.0F, 12.0F });
    float heading = 0.0F;
    float worstLag = 0.0F;
    for (int i = 0; i < 4 * 60; ++i) {  // 180 degrees at 45 degrees a second
        heading += 45.0F * kDegrees * kFrame;
        chase.Step(heading, Moving(heading, 12.0F));
        worstLag = std::max(worstLag, AngleBetween(chase.camera, heading));
    }
    CHECK(worstLag < 60.0F * kDegrees);

    chase.Run(6.0F, heading, Moving(heading, 12.0F));
    CHECK(AngleBetween(chase.camera, heading) < 0.02F);
}

TEST_CASE("when nearly stopped the camera settles behind the skis")
{
    Chase chase;
    chase.Run(kFrame, 0.0F, { 0.0F, 10.0F });
    chase.Run(6.0F, 1.0F, { 0.0F, 0.1F });
    CHECK(AngleBetween(chase.camera, 1.0F) < 0.02F);
}

TEST_CASE("while skidding the camera follows where you'll be heading, not the skis or the stop")
{
    Chase chase;
    chase.Run(kFrame, 0.0F, { 0.0F, 10.0F });
    chase.Run(3.0F, 0.0F, { 0.0F, 10.0F });
    chase.Run(0.5F, kPi / 2.0F, { 0.0F, 2.0F }, 0.0F, 0.0F);  // twisted 90 degrees, slowing
    CHECK(AngleBetween(chase.camera, 0.0F) < 0.01F);
    chase.Run(2.0F, kPi / 2.0F, { 0.0F, 0.0F }, 0.0F, 0.0F);  // stopped, still twisted
    CHECK(AngleBetween(chase.camera, 0.0F) < 0.01F);

    chase.Run(6.0F, kPi / 2.0F, { 0.0F, 0.0F }, 0.0F, kPi);  // parked: heading is now downhill
    CHECK(AngleBetween(chase.camera, kPi) < 0.02F);
}

TEST_CASE("looking around is kept while looking, then eases back")
{
    Chase chase;
    chase.Run(kFrame, 0.0F, { 0.0F, 10.0F });
    chase.Run(0.5F, 0.0F, { 0.0F, 10.0F }, 0.02F);
    const float looked = chase.camera;
    CHECK(looked > 0.5F);

    chase.Run(0.3F, 0.0F, { 0.0F, 10.0F });
    CHECK(AngleBetween(chase.camera, looked) < 0.01F);

    chase.Run(3.0F, 0.0F, { 0.0F, 10.0F });
    CHECK(AngleBetween(chase.camera, 0.0F) < 0.01F);
}

TEST_CASE("following works across the 0 / 2pi seam")
{
    Chase chase;
    chase.camera = 0.1F;
    chase.Run(kFrame, 0.1F, { 0.0F, 0.0F });
    chase.Run(6.0F, 2.0F * kPi - 0.2F, { 0.0F, 0.0F });
    CHECK(AngleBetween(chase.camera, -0.2F) < 0.02F);
    CHECK(chase.camera >= 0.0F && chase.camera < 2.0F * kPi);
}
