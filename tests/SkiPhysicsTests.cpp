#include "Check.h"

#include "core/SkiPhysics.h"

#include <numbers>

using namespace SkiingInSkyrim::Core;

namespace
{
    constexpr float kFrame = 1.0F / 60.0F;
    constexpr float kDegrees = std::numbers::pi_v<float> / 180.0F;
    constexpr float kQuarterTurn = std::numbers::pi_v<float> / 2.0F;
    const Vec3      kFlat{ 0.0F, 0.0F, 1.0F };

    // A slope of the given angle whose downhill direction is +Y (yaw 0).
    Vec3 SlopeDownY(float degrees)
    {
        return { 0.0F, std::sin(degrees * kDegrees), std::cos(degrees * kDegrees) };
    }

    float Heading(Vec2 velocity) { return std::atan2(velocity.x, velocity.y); }

    struct Controls
    {
        float edge{ 0.0F };
        float pivot{ 0.0F };
        bool  keys{ false };
        bool  grounded{ true };
    };

    struct Skier
    {
        SkiState    state;
        SkiSettings settings;
        float       yaw{ 0.0F };
        SkiStep     last;

        // Starts already edged so tests about the physics don't depend on the ramp.
        Skier& Edged(float edge)
        {
            state.edge = edge;
            return *this;
        }

        const SkiStep& Step(Vec3 normal, Controls controls = {})
        {
            SkiInput input;
            input.deltaSeconds = kFrame;
            input.grounded = controls.grounded;
            input.hasNormal = true;
            input.normal = normal;
            input.yaw = yaw;
            input.edgeTarget = controls.edge;
            input.edgeFromKeys = controls.keys;
            input.pivotTarget = controls.pivot;
            last = StepSki(state, input, settings);
            yaw = last.yaw;
            return last;
        }

        void Run(float seconds, Vec3 normal, Controls controls = {})
        {
            for (int i = 0; i < static_cast<int>(seconds / kFrame + 0.5F); ++i) {
                Step(normal, controls);
            }
        }

        float Speed() const { return Length(state.velocity); }
    };
}

TEST_CASE("standing on flat ground stays put")
{
    Skier skier;
    skier.Run(2.0F, kFlat);
    CHECK_NEAR(skier.Speed(), 0.0F, 1e-5F);
}

TEST_CASE("a slope gentler than snow friction does not start a slide")
{
    Skier skier;
    skier.Run(2.0F, SlopeDownY(2.0F));
    CHECK_NEAR(skier.Speed(), 0.0F, 1e-4F);
}

TEST_CASE("pointing down a slope accelerates toward the drag-limited speed")
{
    Skier skier;
    skier.Step(SlopeDownY(20.0F));
    const float expectedAcceleration = 9.81F * (std::sin(20.0F * kDegrees) - 0.05F * std::cos(20.0F * kDegrees));
    CHECK_NEAR(skier.Speed(), expectedAcceleration * kFrame, 1e-3F);

    skier.Run(60.0F, SlopeDownY(20.0F));
    const float terminal = std::sqrt(expectedAcceleration / skier.settings.dragPerSpeedSquared);
    CHECK_NEAR(skier.Speed(), terminal, 0.3F);
    CHECK(skier.state.velocity.y > 0.0F);
}

TEST_CASE("standing across the fall line on edge holds")
{
    Skier skier;
    skier.Edged(1.0F).yaw = kQuarterTurn;  // facing +X, slope falls toward +Y
    skier.Run(2.0F, SlopeDownY(30.0F), { .edge = 1.0F });
    CHECK_NEAR(skier.Speed(), 0.0F, 1e-4F);
}

TEST_CASE("standing across the fall line on flat skis side-slips downhill")
{
    Skier skier;
    skier.yaw = kQuarterTurn;
    skier.Run(1.0F, SlopeDownY(30.0F));
    CHECK(skier.state.velocity.y > 2.0F);
    CHECK(std::fabs(skier.state.velocity.x) < 0.1F);
    CHECK_NEAR(skier.yaw, kQuarterTurn, 1e-5F);
    CHECK(skier.last.skidding);
}

TEST_CASE("too little edge on a steep slope keeps sliding")
{
    Skier light;
    light.Edged(0.3F).yaw = kQuarterTurn;
    light.Run(1.0F, SlopeDownY(40.0F), { .edge = 0.3F });
    CHECK(light.state.velocity.y > 1.0F);

    Skier hard;
    hard.Edged(1.0F).yaw = kQuarterTurn;
    hard.Run(1.0F, SlopeDownY(40.0F), { .edge = 1.0F });
    CHECK_NEAR(hard.Speed(), 0.0F, 1e-4F);
}

TEST_CASE("a clean carve follows the sidecut arc and keeps its speed")
{
    Skier skier;
    skier.Edged(0.6F).state.velocity = { 0.0F, 8.0F };
    skier.Run(1.0F, kFlat, { .edge = 0.6F });

    const float radius = CarveRadius(0.6F * skier.settings.maxEdgeDegrees * kDegrees, skier.settings);
    const float expectedTurn = 7.5F / radius;  // roughly the mean speed over the second
    CHECK_NEAR(skier.yaw, expectedTurn, 0.06F);
    CHECK_NEAR(Heading(skier.state.velocity), skier.yaw, 2.0F * kDegrees);
    CHECK(!skier.last.skidding);

    // Only snow friction and drag slow it; turning itself costs (almost) nothing.
    const float friction = 0.05F * 9.81F + 0.01F * 64.0F;
    CHECK(skier.Speed() > 8.0F - friction - 0.05F);
}

TEST_CASE("more edge carves a tighter arc")
{
    Skier light;
    light.Edged(0.3F).state.velocity = { 0.0F, 6.0F };
    light.Run(1.0F, kFlat, { .edge = 0.3F });

    Skier hard;
    hard.Edged(0.9F).state.velocity = { 0.0F, 6.0F };
    hard.Run(1.0F, kFlat, { .edge = 0.9F });

    CHECK(hard.yaw > light.yaw * 1.3F);
}

TEST_CASE("left edge turns left")
{
    Skier skier;
    skier.Edged(-0.6F).state.velocity = { 0.0F, 8.0F };
    skier.Run(0.5F, kFlat, { .edge = -0.6F });
    CHECK(skier.yaw > std::numbers::pi_v<float>);
    CHECK(Heading(skier.state.velocity) < 0.0F);
}

TEST_CASE("at speed the grip limits how tight the arc can be")
{
    Skier skier;
    skier.Edged(1.0F).state.velocity = { 0.0F, 20.0F };
    skier.Run(0.5F, kFlat, { .edge = 1.0F });

    const float sidecutRate = 20.0F / CarveRadius(skier.settings.maxEdgeDegrees * kDegrees, skier.settings);
    const float gripRate = skier.last.gripLimit / 20.0F;
    CHECK(skier.yaw < sidecutRate * 0.5F * 0.6F);
    CHECK_NEAR(skier.yaw, gripRate * 0.5F, 0.08F);
}

TEST_CASE("edging while stopped does not turn")
{
    Skier skier;
    skier.Run(1.0F, kFlat, { .edge = 1.0F });
    CHECK_NEAR(skier.yaw, 0.0F, 1e-6F);
}

TEST_CASE("hockey stop: skis across the direction of travel on hard edge brake instead of turning")
{
    Skier skier;
    skier.Edged(1.0F).yaw = kQuarterTurn;
    skier.state.velocity = { 0.0F, 10.0F };
    const float grip = 9.81F * (skier.settings.flatGrip +
                                   skier.settings.edgeGrip * std::tan(skier.settings.maxEdgeDegrees * kDegrees));

    skier.Run(0.25F, kFlat, { .edge = 1.0F });
    CHECK(skier.last.skidding);
    CHECK_NEAR(skier.Speed(), 10.0F - (grip + 0.01F * 90.0F) * 0.25F, 0.4F);
    CHECK_NEAR(skier.state.velocity.x, 0.0F, 1e-4F);  // the direction of travel didn't swing round

    skier.Run(1.0F, kFlat, { .edge = 1.0F });
    CHECK_NEAR(skier.Speed(), 0.0F, 1e-4F);
}

TEST_CASE("hockey stop from a pivot: twisting the skis and edging scrubs most of the speed")
{
    Skier stopping;
    stopping.state.velocity = { 0.0F, 10.0F };
    float maxCrosswise = 0.0F;
    for (int i = 0; i < 84; ++i) {
        stopping.Step(kFlat, i < 36 ? Controls{ .pivot = 1.0F } : Controls{ .edge = 1.0F });
        maxCrosswise = std::max(maxCrosswise, std::fabs(stopping.state.velocity.x));
    }

    Skier gliding;
    gliding.state.velocity = { 0.0F, 10.0F };
    gliding.Run(1.4F, kFlat);

    CHECK(stopping.yaw > 70.0F * kDegrees);
    CHECK(stopping.Speed() < 3.0F);
    CHECK(gliding.Speed() > 8.0F);
    CHECK(maxCrosswise < 2.0F);  // the skier never swung round to travel where the skis point
}

TEST_CASE("flat skis across the direction of travel only smear")
{
    Skier skier;
    skier.yaw = kQuarterTurn;
    skier.state.velocity = { 0.0F, 10.0F };
    skier.Run(1.0F, kFlat);
    CHECK(skier.Speed() > 8.0F);
    CHECK(skier.last.skidding);
}

TEST_CASE("pivoting is harder on edge")
{
    Skier flat;
    flat.Run(0.5F, kFlat, { .pivot = 1.0F });

    Skier edged;
    edged.Edged(1.0F).Run(0.5F, kFlat, { .edge = 1.0F, .pivot = 1.0F });

    CHECK(flat.yaw > 0.5F);
    CHECK_NEAR(edged.yaw / flat.yaw, edged.settings.pivotEdgedFactor, 0.02F);
}

TEST_CASE("a tapped edge key leaves partial edge that releases slowly")
{
    Skier skier;
    skier.Run(0.1F, kFlat, { .edge = 1.0F, .keys = true });
    const float tapped = skier.state.edge;
    CHECK_NEAR(tapped, skier.settings.keyEdgeRamp * 0.1F, 1e-3F);

    skier.Run(0.1F, kFlat, { .edge = 0.0F, .keys = true });
    CHECK_NEAR(skier.state.edge, tapped - skier.settings.keyEdgeRelease * 0.1F, 1e-3F);
    CHECK(skier.state.edge > 0.0F);
}

TEST_CASE("the stick sets edge quickly in both directions")
{
    Skier skier;
    skier.Run(0.2F, kFlat, { .edge = 0.7F });
    CHECK_NEAR(skier.state.edge, 0.7F, 1e-5F);
    skier.Run(0.2F, kFlat, { .edge = 0.0F });
    CHECK_NEAR(skier.state.edge, 0.0F, 1e-5F);
}

TEST_CASE("when the engine delivers much less than commanded, the model gives way")
{
    Skier skier;
    skier.state.velocity = { 0.0F, 10.0F };
    skier.state.commandedSpeed = 10.0F;
    SkiInput input;
    input.deltaSeconds = kFrame;
    input.hasNormal = true;
    input.normal = kFlat;
    input.hasObserved = true;
    input.observed = { 0.0F, 1.0F };
    const auto step = StepSki(skier.state, input, skier.settings);
    CHECK(step.blocked);
    CHECK(skier.Speed() < 7.5F);
}

TEST_CASE("normal engine lag is not mistaken for a block")
{
    Skier skier;
    skier.state.velocity = { 0.0F, 10.0F };
    skier.state.commandedSpeed = 10.0F;
    SkiInput input;
    input.deltaSeconds = kFrame;
    input.hasNormal = true;
    input.normal = SlopeDownY(15.0F);
    input.hasObserved = true;
    input.observed = { 0.0F, 9.0F };
    CHECK(!StepSki(skier.state, input, skier.settings).blocked);
}

TEST_CASE("in the air, momentum is kept apart from drag and nothing steers it")
{
    Skier skier;
    skier.Edged(1.0F).state.velocity = { 0.0F, 10.0F };
    skier.Run(0.5F, SlopeDownY(30.0F), { .edge = 1.0F, .pivot = 1.0F, .grounded = false });
    CHECK(skier.Speed() < 10.0F);
    CHECK(skier.Speed() > 9.0F);
    CHECK_NEAR(skier.state.velocity.x, 0.0F, 1e-5F);
}

TEST_CASE("speed is capped")
{
    Skier skier;
    skier.state.velocity = { 0.0F, 100.0F };
    skier.Step(SlopeDownY(45.0F));
    CHECK_NEAR(skier.Speed(), skier.settings.maxSpeed, 1e-3F);
}

TEST_CASE("yaw stays in [0, 2pi)")
{
    Skier skier;
    skier.yaw = 0.01F;
    skier.state.pivot = -1.0F;
    skier.Step(kFlat, { .pivot = -1.0F });
    CHECK(skier.yaw >= 0.0F && skier.yaw < 2.0F * std::numbers::pi_v<float>);
    CHECK(skier.yaw > 6.0F);
}
