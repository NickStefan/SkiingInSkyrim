#include "Check.h"

#include "core/TumbleCamera.h"

#include <numbers>

using namespace SkiingInSkyrim::Core;

namespace
{
    constexpr float kFrame = 1.0F / 60.0F;
}

TEST_CASE("the tumble shot holds still at the hit, then drifts after the body")
{
    TumbleCameraSettings settings;
    TumbleCameraState    state;
    StartTumbleCamera(state, { 0.0F, -600.0F, 200.0F }, { 0.0F, 0.0F, 0.0F });
    for (int i = 0; i < 12; ++i) {  // 0.2 s, inside the freeze
        UpdateTumbleCamera(state, settings, kFrame, { 0.0F, 50.0F * static_cast<float>(i), 0.0F }, std::nullopt);
    }
    CHECK(state.camera.y == -600.0F);
    CHECK(state.look.y > 0.0F);

    for (int i = 0; i < 1200; ++i) {  // the body comes to rest 1000 units on
        UpdateTumbleCamera(state, settings, kFrame, { 0.0F, 1000.0F, 0.0F }, std::nullopt);
    }
    CHECK_NEAR(state.camera.y, 400.0F, 1.0F);
    CHECK_NEAR(state.camera.z, 200.0F, 1.0F);
    CHECK_NEAR(state.look.y, 1000.0F, 1.0F);
}

TEST_CASE("the tumble shot stays above the ground")
{
    TumbleCameraSettings settings;
    TumbleCameraState    state;
    StartTumbleCamera(state, { 0.0F, -600.0F, 50.0F }, { 0.0F, 0.0F, 0.0F });
    UpdateTumbleCamera(state, settings, kFrame, { 0.0F, 0.0F, 0.0F }, 20.0F);
    CHECK_NEAR(state.camera.z, 20.0F + settings.clearanceUnits, 1e-3F);
}

TEST_CASE("the tumble shot comes in front of what blocks the view at once, and eases back out")
{
    TumbleCameraSettings settings;
    TumbleCameraState    state;
    StartTumbleCamera(state, { 0.0F, -600.0F, 0.0F }, { 0.0F, 0.0F, 0.0F });
    PlaceTumbleCamera(state, settings, kFrame, 300.0F);  // a rock 300 units out from the body
    CHECK_NEAR(state.shown.y, -(300.0F - settings.obstructionMarginUnits), 1e-2F);

    PlaceTumbleCamera(state, settings, kFrame, std::nullopt);
    CHECK(state.shown.y < -275.0F);
    CHECK(state.shown.y > -300.0F);  // not straight back out
    for (int i = 0; i < 600; ++i) {
        PlaceTumbleCamera(state, settings, kFrame, std::nullopt);
    }
    CHECK_NEAR(state.shown.y, -600.0F, 1.0F);

    PlaceTumbleCamera(state, settings, kFrame, 10.0F);  // right against the body
    CHECK_NEAR(state.shown.y, -settings.closestUnits, 1e-2F);
}

TEST_CASE("looking from one point to another gives the game's heading and a downward pitch")
{
    const auto north = LookFrom({ 0.0F, 0.0F, 100.0F }, { 0.0F, 100.0F, 0.0F });
    CHECK_NEAR(north.yaw, 0.0F, 1e-4F);
    CHECK_NEAR(north.pitch, std::numbers::pi_v<float> / 4.0F, 1e-4F);
    const auto east = LookFrom({ 0.0F, 0.0F, 0.0F }, { 100.0F, 0.0F, 0.0F });
    CHECK_NEAR(east.yaw, std::numbers::pi_v<float> / 2.0F, 1e-4F);
    CHECK_NEAR(east.pitch, 0.0F, 1e-4F);
}
