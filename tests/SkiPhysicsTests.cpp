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
    float AngleBetweenRadians(float a, float b) { return std::fabs(std::remainder(a - b, 2.0F * std::numbers::pi_v<float>)); }
    Vec2  Scale2(Vec2 v, float s) { return { v.x * s, v.y * s }; }

    struct Controls
    {
        float edge{ 0.0F };
        bool  skid{ false };
        float step{ 0.0F };
        bool  buttons{ false };
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
            input.edgeFromButtons = controls.buttons;
            input.skidHeld = controls.skid;
            input.stepTurn = controls.step;
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
    const float friction = 0.05F * 9.81F + skier.settings.dragPerSpeedSquared * 64.0F;
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
    CHECK_NEAR(skier.Speed(), 10.0F - (grip + skier.settings.dragPerSpeedSquared * 90.0F) * 0.25F, 0.4F);
    CHECK_NEAR(skier.state.velocity.x, 0.0F, 1e-4F);  // the direction of travel didn't swing round

    skier.Run(1.0F, kFlat, { .edge = 1.0F });
    CHECK_NEAR(skier.Speed(), 0.0F, 1e-4F);
}

TEST_CASE("flat skis across the direction of travel only smear")
{
    Skier skier;
    skier.yaw = kQuarterTurn;
    skier.state.velocity = { 0.0F, 10.0F };
    skier.Run(1.0F, kFlat);
    CHECK(skier.Speed() > 6.5F);
    CHECK(skier.last.skidding);
}

TEST_CASE("hockey stop: holding pivot on a hard edge stops in one motion")
{
    Skier stopping;
    stopping.state.velocity = { 0.0F, 10.0F };
    float maxCrosswise = 0.0F;
    for (int i = 0; i < 90; ++i) {
        stopping.Step(kFlat, { .edge = 1.0F, .skid = true });
        maxCrosswise = std::max(maxCrosswise, std::fabs(stopping.state.velocity.x));
    }

    CHECK_NEAR(stopping.last.pivotDegrees, 90.0F, 0.5F);
    CHECK(stopping.Speed() < 1.0F);
    CHECK(maxCrosswise < 2.5F);  // braked rather than swinging round to where the skis point
}

TEST_CASE("pivot twists toward the edge being held")
{
    Skier right;
    right.state.velocity = { 0.0F, 10.0F };
    right.Run(0.15F, kFlat, { .edge = 1.0F, .skid = true });

    Skier left;
    left.state.velocity = { 0.0F, 10.0F };
    left.Run(0.15F, kFlat, { .edge = -1.0F, .skid = true });

    CHECK(right.last.pivotDegrees > 30.0F);
    CHECK(left.last.pivotDegrees < -30.0F);
}

TEST_CASE("with flat skis, pivot twists toward the last edge used")
{
    Skier skier;
    skier.Run(0.3F, kFlat, { .edge = -1.0F });
    skier.Run(0.5F, kFlat);
    skier.Run(0.1F, kFlat, { .skid = true });
    CHECK(skier.last.pivotDegrees < 0.0F);
}

TEST_CASE("a short hold gives a partial pivot")
{
    Skier skier;
    skier.Run(0.1F, kFlat, { .skid = true });
    const float expected = skier.settings.pivotMaxDegrees * 0.1F / skier.settings.pivotBuildSeconds;
    CHECK_NEAR(skier.last.pivotDegrees, expected, 1.0F);
}

TEST_CASE("releasing a skid that only slowed you untwists at the normal rate")
{
    Skier skier;
    skier.yaw = 0.2F;
    skier.state.velocity = { std::sin(0.2F) * 8.0F, std::cos(0.2F) * 8.0F };
    skier.Run(0.15F, kFlat, { .skid = true });
    CHECK(skier.yaw > 0.5F);
    CHECK(!skier.last.parked);
    skier.Run(skier.settings.pivotReturnSeconds + 0.05F, kFlat);
    CHECK_NEAR(skier.yaw, 0.2F, 1e-4F);
    CHECK_NEAR(skier.last.pivotDegrees, 0.0F, 1e-4F);
}

namespace
{
    // Skis down a slope at speed and holds skid until stopped and parked.
    Skier ParkedOn(Vec3 slope)
    {
        Skier skier;
        skier.state.velocity = { 0.0F, 8.0F };
        skier.Run(3.0F, slope, { .skid = true });
        return skier;
    }
}

TEST_CASE("stopping with skid parks you")
{
    const auto skier = ParkedOn(SlopeDownY(20.0F));
    CHECK(skier.Speed() < 0.05F);
    CHECK(skier.last.parked);
}

TEST_CASE("releasing skid after a stop side-slips before the skis come round downhill")
{
    auto skier = ParkedOn(SlopeDownY(20.0F));
    skier.Run(0.3F, SlopeDownY(20.0F));
    CHECK(std::fabs(skier.last.pivotDegrees) > 50.0F);  // a normal release would be nearly untwisted
    CHECK(skier.state.velocity.y > 0.5F);               // sliding downhill
    CHECK(std::fabs(skier.last.slipDegrees) > 45.0F);   // sideways

    skier.Run(3.0F, SlopeDownY(20.0F));
    CHECK_NEAR(skier.last.pivotDegrees, 0.0F, 1e-4F);
    CHECK(!skier.last.parked);
    CHECK(skier.Speed() > 2.0F);
}

TEST_CASE("after a stop, an edge holds you across the hill and easing it off slips")
{
    auto skier = ParkedOn(SlopeDownY(30.0F));
    skier.Run(1.0F, SlopeDownY(30.0F), { .edge = 1.0F });
    CHECK(skier.Speed() < 0.05F);
    CHECK(std::fabs(skier.last.pivotDegrees) > 85.0F);

    skier.Run(0.5F, SlopeDownY(30.0F), { .edge = 0.2F });
    CHECK(skier.state.velocity.y > 0.5F);
    CHECK(skier.last.parked);
    CHECK(std::fabs(skier.last.pivotDegrees) > 85.0F);  // still across the hill

    skier.Run(1.0F, SlopeDownY(30.0F), { .edge = 1.0F });  // dig back in
    CHECK(skier.Speed() < 0.05F);
}

TEST_CASE("side-slipping, the trigger feathers the slip on gentle and steep slopes alike")
{
    for (const float degrees : { 10.0F, 30.0F }) {
        const auto slope = SlopeDownY(degrees);
        auto held = ParkedOn(slope);
        held.Run(1.0F, slope, { .edge = 1.0F });

        auto half = ParkedOn(slope);
        half.Run(1.0F, slope, { .edge = 0.5F });

        auto flat = ParkedOn(slope);
        flat.Run(1.0F, slope);

        CHECK(held.Speed() < 0.05F);
        CHECK(half.Speed() > flat.Speed() * 0.2F);
        CHECK(half.Speed() < flat.Speed() * 0.8F);
        CHECK(std::fabs(half.last.pivotDegrees) > 85.0F);  // still across the hill
    }
}

TEST_CASE("ground hug keeps you from rising off the slope you left and pulls you down")
{
    SkiSettings settings;
    const auto  slope = SlopeDownY(30.0F);
    const Vec3  launched{ 0.0F, 10.0F, 3.0F };  // skiing down at 10 m/s, kicked upward off a crest
    const float alongSlope = -10.0F * std::tan(30.0F * kDegrees);
    const float vertical = GroundHugVerticalSpeed(launched, slope, kFrame, settings);
    CHECK_NEAR(vertical, alongSlope - settings.groundHugGravity * kFrame, 1e-4F);

    const Vec3 falling{ 0.0F, 10.0F, -12.0F };  // already dropping well past the slope: no extra pull
    CHECK_NEAR(GroundHugVerticalSpeed(falling, slope, kFrame, settings), -12.0F, 1e-4F);

    Vec3 hugged = launched;  // held over several frames, the pull stops a little past following the slope
    for (int i = 0; i < 30; ++i) {
        hugged.z = GroundHugVerticalSpeed(hugged, slope, kFrame, settings);
    }
    CHECK_NEAR(hugged.z, alongSlope - settings.groundHugMaxDropSpeed, 1e-4F);
}

TEST_CASE("on flat ground a parked twist comes back on its own")
{
    Skier skier;
    skier.Run(0.5F, kFlat, { .skid = true });
    CHECK(skier.last.parked);
    skier.Run(skier.settings.sideslipSeconds + skier.settings.sideslipTurnSeconds + 0.1F, kFlat);
    CHECK_NEAR(skier.yaw, 0.0F, 1e-4F);
    CHECK(!skier.last.parked);
}

TEST_CASE("on flat ground, turning on the spot ends a stop where the skis are")
{
    Skier skier;
    skier.Run(0.5F, kFlat, { .skid = true });
    CHECK(skier.last.parked);
    const float before = skier.yaw;
    skier.Step(kFlat, { .step = 1.0F });
    CHECK(!skier.last.parked);
    CHECK_NEAR(skier.last.pivotDegrees, 0.0F, 1e-6F);
    CHECK(std::fabs(std::remainder(skier.yaw - before, 2.0F * std::numbers::pi_v<float>)) < 0.05F);
}

TEST_CASE("you stop facing across the hill on the side of the held trigger")
{
    for (const float side : { -1.0F, 1.0F }) {
        Skier skier;  // straight down a 20 degree slope
        skier.state.velocity = { 0.0F, 8.0F };
        skier.Run(3.0F, SlopeDownY(20.0F), { .edge = side, .skid = true });
        CHECK(skier.last.parked);
        CHECK_NEAR(skier.last.pivotDegrees, side * 90.0F, 0.5F);
    }
}

TEST_CASE("side-slipping, the other trigger swings the skis through the fall line to the other side")
{
    const auto slope = SlopeDownY(30.0F);
    auto       skier = ParkedOn(slope);
    skier.Run(0.5F, slope, { .edge = 1.0F });
    CHECK_NEAR(skier.last.pivotDegrees, 90.0F, 0.5F);

    float widest = 0.0F;  // furthest from downhill during the swing: through uphill would pass 180
    for (int i = 0; i < 60; ++i) {
        skier.Step(slope, { .edge = -1.0F });
        widest = std::max(widest, std::fabs(skier.last.pivotDegrees));
    }
    CHECK(skier.last.parked);
    CHECK_NEAR(skier.last.pivotDegrees, -90.0F, 0.5F);
    CHECK(widest < 91.0F);

    CHECK(skier.Speed() < 1.5F);  // a little speed from passing through the fall line, now along the skis
    skier.Run(2.5F, slope, { .edge = -1.0F });
    CHECK(skier.Speed() < 0.05F);  // the new side's trigger holds
}

TEST_CASE("side-slipping fast, the other trigger doesn't swing the skis round")
{
    const auto slope = SlopeDownY(35.0F);
    auto       skier = ParkedOn(slope);
    skier.Run(1.0F, slope);  // sliding sideways on flat skis
    CHECK(skier.Speed() > 2.0F);
    skier.Run(0.3F, slope, { .edge = -1.0F });
    CHECK(skier.last.parked);
    CHECK_NEAR(skier.last.pivotDegrees, 90.0F, 1.0F);
}

TEST_CASE("side-slipping, a light touch of the other trigger doesn't grip or switch sides")
{
    const auto slope = SlopeDownY(30.0F);
    auto       wrong = ParkedOn(slope);
    wrong.Run(0.5F, slope, { .edge = -0.2F });
    auto flat = ParkedOn(slope);
    flat.Run(0.5F, slope);
    CHECK_NEAR(wrong.Speed(), flat.Speed(), 0.05F);
    CHECK_NEAR(wrong.last.pivotDegrees, 90.0F, 0.5F);
}

TEST_CASE("side-slipping, the stick ends it and aims the skis downhill that way")
{
    for (const float stick : { -1.0F, 1.0F }) {
        const auto slope = SlopeDownY(20.0F);
        auto       skier = ParkedOn(slope);
        skier.Run(0.2F, slope, { .edge = 1.0F });
        skier.Step(slope, { .edge = 1.0F, .step = stick });
        CHECK(skier.last.parked);
        CHECK(skier.last.nextHeading.has_value());
        CHECK(AngleBetweenRadians(*skier.last.nextHeading, stick * 45.0F * kDegrees) < 1e-3F);

        skier.Run(skier.settings.sideslipTurnSeconds * 1.5F + 0.1F, slope);  // up to 135 degrees round
        CHECK(!skier.last.parked);
        CHECK(AngleBetweenRadians(skier.yaw, stick * 45.0F * kDegrees) < 0.05F);
    }

    const auto slope = SlopeDownY(20.0F);
    auto       brushed = ParkedOn(slope);
    brushed.Run(0.5F, slope, { .edge = 1.0F, .step = 0.3F });
    CHECK(brushed.last.parked);
    CHECK_NEAR(brushed.last.pivotDegrees, 90.0F, 0.5F);
}

TEST_CASE("holding pivot on the uphill edge keeps you standing on a slope")
{
    Skier skier;  // facing straight down a 15 degree slope
    skier.Run(1.5F, SlopeDownY(15.0F), { .edge = 1.0F, .skid = true });
    CHECK(skier.Speed() < 0.05F);

    skier.Run(3.0F, SlopeDownY(15.0F));
    CHECK(skier.Speed() > 2.0F);  // let go and you're skiing again
}

TEST_CASE("while moving, pivot is limited to 90 degrees from the direction of travel")
{
    Skier skier;
    skier.settings.pivotMaxDegrees = 150.0F;
    skier.state.velocity = { 0.0F, 10.0F };
    float maxSlip = 0.0F;
    while (skier.Speed() >= 1.0F) {
        skier.Step(kFlat, { .skid = true });
        const float slip = std::atan2(skier.state.velocity.x, skier.state.velocity.y) - skier.yaw;
        maxSlip = std::max(maxSlip, std::fabs(std::remainder(slip, 2.0F * std::numbers::pi_v<float>)));
    }
    CHECK(maxSlip <= kQuarterTurn + 1e-3F);
}

TEST_CASE("skid with no edge stops you and holds you on the hill")
{
    Skier skier;  // skiing straight down a 20 degree slope
    skier.state.velocity = { 0.0F, 8.0F };
    skier.Run(3.0F, SlopeDownY(20.0F), { .skid = true });
    CHECK(skier.Speed() < 0.05F);

    skier.Run(3.0F, SlopeDownY(20.0F));
    CHECK(skier.Speed() > 2.0F);  // let go and you're skiing again
}

TEST_CASE("skid stops you on a steep slope even when the skis were already sliding sideways")
{
    Skier skier;  // 40 degree slope falling toward +Y; travelling 60 degrees right of it, skis 75 degrees left of that
    const float travel = 60.0F * kDegrees;
    skier.yaw = travel - 75.0F * kDegrees;
    skier.state.velocity = Scale2(Forward(travel), 10.0F);
    float worstSlip = 0.0F;
    for (int i = 0; i < 4 * 60; ++i) {
        skier.Step(SlopeDownY(40.0F), { .edge = -1.0F, .skid = true });
        if (skier.Speed() > 1.0F) {
            worstSlip = std::max(worstSlip, std::fabs(skier.last.slipDegrees));
        }
    }
    CHECK(skier.Speed() < 0.05F);
    CHECK(worstSlip < 95.0F);  // never sliding backwards along the skis
}

TEST_CASE("carving hard past across a steep slope skids to a stop instead of skiing backwards")
{
    Skier skier;  // 45 degree slope falling toward +Y; traversing toward +X at 12 m/s, full right edge turns uphill
    skier.yaw = kQuarterTurn;
    skier.state.velocity = { 12.0F, 0.0F };
    float worstSlip = 0.0F;
    for (int i = 0; i < 6 * 60; ++i) {
        skier.Step(SlopeDownY(45.0F), { .edge = 1.0F });
        if (skier.Speed() > 1.0F) {
            worstSlip = std::max(worstSlip, std::fabs(skier.last.slipDegrees));
        }
    }
    CHECK(worstSlip < 95.0F);
    CHECK(skier.Speed() < 0.2F);  // stopped across the hill on the edge, perhaps creeping
}

TEST_CASE("side-slipping where the fall line swings round, you don't drift backwards along the skis")
{
    // Slope falling toward +Y, then swinging 60 degrees round over a second, as when sliding across a bowl.
    const auto slopeToward = [](float degreesRound) {
        const float slope = 40.0F * kDegrees;
        const float round = degreesRound * kDegrees;
        return Vec3{ std::sin(round) * std::sin(slope), std::cos(round) * std::sin(slope), std::cos(slope) };
    };
    auto skier = ParkedOn(slopeToward(0.0F));
    skier.Run(0.5F, slopeToward(0.0F));
    float worstSlip = 0.0F;
    for (int i = 0; i < 60; ++i) {
        skier.Step(slopeToward(static_cast<float>(i)), { .edge = 0.3F });
        worstSlip = std::max(worstSlip, std::fabs(skier.last.slipDegrees));
    }
    for (int i = 0; i < 3 * 60; ++i) {
        skier.Step(slopeToward(60.0F), { .edge = 1.0F });
        if (skier.Speed() > 0.3F) {
            worstSlip = std::max(worstSlip, std::fabs(skier.last.slipDegrees));
        }
    }
    CHECK(worstSlip < 95.0F);
    CHECK(skier.Speed() < 0.05F);
}

TEST_CASE("parked, the skis settle across the hill")
{
    auto skier = ParkedOn(SlopeDownY(20.0F));
    CHECK_NEAR(std::fabs(skier.last.pivotDegrees), 90.0F, 0.5F);
    CHECK(skier.last.nextHeading.has_value());
    CHECK(AngleBetweenRadians(*skier.last.nextHeading, 0.0F) < 1e-3F);  // downhill
}

TEST_CASE("stopping while facing uphill comes out pointing downhill, not skiing backwards")
{
    Skier skier;  // slowing to a stop, skis pointing mostly up a slope that falls toward +Y
    skier.yaw = 150.0F * kDegrees;
    skier.state.velocity = Scale2(Forward(skier.yaw), 2.0F);
    skier.Run(2.0F, SlopeDownY(20.0F), { .skid = true });
    CHECK(skier.last.parked);

    skier.Run(3.0F, SlopeDownY(20.0F));
    CHECK(!skier.last.parked);
    CHECK(AngleBetweenRadians(skier.yaw, 0.0F) < 0.1F);
    CHECK(skier.Speed() > 1.0F);
    CHECK(Dot(skier.state.velocity, Forward(skier.yaw)) > 0.0F);
}

TEST_CASE("skid with no edge twists into the hill and doesn't slide backwards from a traverse")
{
    Skier skier;  // traversing toward +X, the slope falls toward +Y on the skier's left
    skier.yaw = kQuarterTurn;
    skier.state.velocity = { 6.0F, 0.0F };
    skier.Run(0.15F, SlopeDownY(25.0F), { .skid = true });
    CHECK(skier.last.pivotDegrees > 0.0F);  // right, uphill

    skier.Run(3.0F, SlopeDownY(25.0F), { .skid = true });
    CHECK(skier.Speed() < 0.05F);
}

TEST_CASE("skid digs the edges in without any edge input")
{
    Skier skidding;
    skidding.state.velocity = { 0.0F, 10.0F };
    skidding.Run(1.0F, kFlat, { .skid = true });

    Skier smearing;  // flat skis held across the direction of travel only smear
    smearing.yaw = kQuarterTurn;
    smearing.state.velocity = { 0.0F, 10.0F };
    smearing.Run(1.0F, kFlat);

    CHECK(skidding.Speed() < 2.0F);
    CHECK(smearing.Speed() > 6.5F);
}

TEST_CASE("skid brings skis already past the limit back across travel, without snapping")
{
    Skier skier;
    skier.yaw = 2.0F;  // about 115 degrees right of travel
    skier.state.velocity = { 0.0F, 10.0F };
    skier.Step(kFlat, { .skid = true });
    CHECK(skier.yaw < 2.0F);
    CHECK(skier.yaw > 2.0F - 0.1F);

    skier.Run(0.3F, kFlat, { .skid = true });
    const float offset = std::remainder(skier.yaw - Heading(skier.state.velocity), 2.0F * std::numbers::pi_v<float>);
    CHECK(std::fabs(offset) <= kQuarterTurn + 1e-3F);
}

TEST_CASE("stepping around works only when nearly stopped")
{
    Skier stopped;
    stopped.Run(1.0F, kFlat, { .step = 1.0F });
    CHECK_NEAR(stopped.yaw, stopped.settings.stepTurnDegreesPerSecond * kDegrees, 0.02F);

    Skier moving;
    moving.state.velocity = { 0.0F, 5.0F };
    moving.Run(0.5F, kFlat, { .step = 1.0F });
    CHECK_NEAR(moving.yaw, 0.0F, 1e-5F);
}

TEST_CASE("a tapped edge button leaves partial edge that releases slowly")
{
    Skier skier;
    skier.Run(0.1F, kFlat, { .edge = 1.0F, .buttons = true });
    const float tapped = skier.state.edge;
    CHECK_NEAR(tapped, skier.settings.buttonEdgeRamp * 0.1F, 1e-3F);

    skier.Run(0.1F, kFlat, { .edge = 0.0F, .buttons = true });
    CHECK_NEAR(skier.state.edge, tapped - skier.settings.buttonEdgeRelease * 0.1F, 1e-3F);
    CHECK(skier.state.edge > 0.0F);
}

TEST_CASE("the triggers set edge quickly in both directions")
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
    skier.Run(0.5F, SlopeDownY(30.0F), { .edge = 1.0F, .skid = true, .grounded = false });
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
    skier.Step(kFlat, { .step = -1.0F });
    CHECK(skier.yaw >= 0.0F && skier.yaw < 2.0F * std::numbers::pi_v<float>);
    CHECK(skier.yaw > 6.0F);
}
