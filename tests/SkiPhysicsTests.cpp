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
        float aim{ 0.0F };     // steering strength, 0..1
        float aimYaw{ 0.0F };  // where the stick aims
        float push{ 0.0F };    // push strength beyond aim (keys)
        bool  buttons{ false };
        bool  grounded{ true };
        bool  tired{ false };
    };

    // Packed snow: most tests are about the base model.
    SkiSettings Packed()
    {
        SkiSettings settings;
        settings.powder = false;
        return settings;
    }

    struct Skier
    {
        SkiState    state;
        SkiSettings settings{ Packed() };
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
            input.aimStrength = controls.aim;
            input.aimYaw = controls.aimYaw;
            input.pushStrength = controls.push;
            input.tired = controls.tired;
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
    // Solves dragPerSpeedSquared * v^2 + dragPerSpeedFourth * v^4 = acceleration for v^2.
    const float a = skier.settings.dragPerSpeedSquared;
    const float c = skier.settings.dragPerSpeedFourth;
    const float terminal = std::sqrt((std::sqrt(a * a + 4.0F * c * expectedAcceleration) - a) / (2.0F * c));
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
    CHECK_NEAR(skier.yaw, kQuarterTurn, 3.0F * kDegrees);  // only just fast enough to start self-aligning
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
    const float friction = 0.05F * 9.81F + skier.settings.dragPerSpeedSquared * 64.0F +
                           skier.settings.dragPerSpeedFourth * 4096.0F;
    CHECK(skier.Speed() > 8.0F - friction - 0.05F);
}

TEST_CASE("a carve reports the turn's sideways pull; a straight run reports none")
{
    Skier right;
    right.Edged(0.6F).state.velocity = { 0.0F, 8.0F };
    right.Run(0.5F, kFlat, { .edge = 0.6F });
    const float radius = CarveRadius(0.6F * right.settings.maxEdgeDegrees * kDegrees, right.settings);
    const float speed = right.Speed();
    CHECK_NEAR(right.last.turnAcceleration, speed * speed / radius, 0.1F * speed * speed / radius);

    Skier left;
    left.Edged(-0.6F).state.velocity = { 0.0F, 8.0F };
    left.Run(0.5F, kFlat, { .edge = -0.6F });
    CHECK(left.last.turnAcceleration < 0.0F);

    Skier straight;
    straight.state.velocity = { 0.0F, 8.0F };
    straight.Run(0.5F, SlopeDownY(15.0F));
    CHECK_NEAR(straight.last.turnAcceleration, 0.0F, 1e-3F);
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
    CHECK_NEAR(skier.Speed(), 10.0F - (grip + skier.settings.dragPerSpeedSquared * 90.0F +
                                                    skier.settings.dragPerSpeedFourth * 8100.0F) * 0.25F, 0.4F);
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

    CHECK_NEAR(AngleBetweenRadians(stopping.yaw, 0.0F), kQuarterTurn, 0.01F);
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
    skier.state.velocity = { 0.0F, 10.0F };
    skier.Run(0.3F, kFlat, { .edge = -1.0F });
    skier.Run(0.5F, kFlat);
    skier.Run(0.1F, kFlat, { .skid = true });
    CHECK(skier.last.pivotDegrees < 0.0F);
}

TEST_CASE("a short hold gives a partial pivot")
{
    Skier skier;
    skier.state.velocity = { 0.0F, 10.0F };
    skier.Run(0.1F, kFlat, { .skid = true });
    const float expected = skier.settings.pivotMaxDegrees * 0.1F / skier.settings.pivotBuildSeconds;
    CHECK_NEAR(skier.last.pivotDegrees, expected, 1.0F);
}

TEST_CASE("releasing a skid that only slowed you untwists at the normal rate")
{
    Skier skier;
    skier.settings.selfAlignDegreesPerSecond = 0.0F;
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

TEST_CASE("stopping with skid holds you there")
{
    const auto skier = ParkedOn(SlopeDownY(20.0F));
    CHECK(skier.Speed() < 0.05F);
    CHECK(skier.last.parked);
    CHECK_NEAR(AngleBetweenRadians(skier.yaw, 0.0F), kQuarterTurn, 2.0F * kDegrees);  // twisted across the hill
}

TEST_CASE("releasing skid after a stop leaves the skis where they are")
{
    auto        skier = ParkedOn(SlopeDownY(20.0F));
    const float stopped = skier.yaw;
    skier.Run(1.0F, SlopeDownY(20.0F), { .edge = 1.0F });
    CHECK(!skier.last.parked);
    CHECK(AngleBetweenRadians(skier.yaw, stopped) < 0.01F);
    CHECK(skier.Speed() < 0.05F);
}

TEST_CASE("after a stop, the uphill edge holds you across the hill and easing it off side-slips")
{
    auto skier = ParkedOn(SlopeDownY(30.0F));  // skis twisted right: right edge uphill
    skier.Run(1.0F, SlopeDownY(30.0F), { .edge = 1.0F });
    CHECK(skier.Speed() < 0.05F);

    skier.Run(0.5F, SlopeDownY(30.0F), { .edge = 0.2F });
    CHECK(skier.state.velocity.y > 0.5F);
    CHECK_NEAR(AngleBetweenRadians(skier.yaw, 0.0F), kQuarterTurn, 2.0F * kDegrees);  // still across the hill
    CHECK(std::fabs(skier.last.slipDegrees) > 85.0F);

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
        half.Run(1.0F, slope, { .edge = 0.3F });

        auto flat = ParkedOn(slope);
        flat.Run(1.0F, slope);

        CHECK(held.Speed() < 0.05F);
        CHECK(half.Speed() > flat.Speed() * 0.2F);
        CHECK(half.Speed() < flat.Speed() * 0.8F);
    }
}

TEST_CASE("side-slipping, the downhill edge doesn't grip")
{
    const auto slope = SlopeDownY(30.0F);
    auto       wrong = ParkedOn(slope);
    wrong.Run(0.5F, slope, { .edge = -1.0F });
    auto flat = ParkedOn(slope);
    flat.Run(0.5F, slope);
    CHECK_NEAR(wrong.Speed(), flat.Speed(), 0.05F);
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

TEST_CASE("the stick's push sets how firmly it steers, ignoring a resting thumb")
{
    const float deadzone = SkiSettings{}.aimDeadzone;
    CHECK_NEAR(AimStrength(0.1F, 0.1F, deadzone), 0.0F, 1e-6F);
    CHECK_NEAR(AimStrength(0.0F, 1.0F, deadzone), 1.0F, 1e-6F);
    CHECK_NEAR(AimStrength(0.8F, 0.8F, deadzone), 1.0F, 1e-6F);  // diagonals reach full too
    const float half = AimStrength(0.0F, 0.6F, deadzone);
    CHECK(half > 0.0F);
    CHECK(half < 0.5F);
}

TEST_CASE("steering turns the skis toward the aim at the steering rate, then holds them there")
{
    Skier skier;
    skier.Run(0.25F, kFlat, { .aim = 1.0F, .aimYaw = kQuarterTurn });
    CHECK_NEAR(skier.yaw, skier.settings.steerDegreesPerSecond * 0.25F * kDegrees, 0.01F);
    skier.Run(1.0F, kFlat, { .aim = 1.0F, .aimYaw = kQuarterTurn });
    CHECK_NEAR(skier.yaw, kQuarterTurn, 1e-4F);

    Skier gentle;
    gentle.Run(0.25F, kFlat, { .aim = 0.25F, .aimYaw = kQuarterTurn });
    CHECK_NEAR(gentle.yaw, skier.settings.steerDegreesPerSecond * 0.25F * 0.25F * kDegrees, 0.01F);
}

TEST_CASE("stopped, aiming behind turns you round on the spot")
{
    Skier skier;
    skier.Run(2.0F, kFlat, { .aim = 1.0F, .aimYaw = 2.0F * kQuarterTurn });
    CHECK(AngleBetweenRadians(skier.yaw, 2.0F * kQuarterTurn) < 1e-3F);
    CHECK(skier.Speed() < 1e-4F);
}

TEST_CASE("after a stop, steering the tips downhill shoots you diagonally across the slope")
{
    const auto  slope = SlopeDownY(30.0F);
    auto        skier = ParkedOn(slope);  // across the hill, facing +X
    const float diagonal = 45.0F * kDegrees;
    skier.Run(2.0F, slope, { .edge = 0.5F, .aim = 1.0F, .aimYaw = diagonal });
    CHECK(skier.Speed() > 3.0F);
    CHECK(AngleBetweenRadians(Heading(skier.state.velocity), diagonal) < 10.0F * kDegrees);
}

TEST_CASE("aiming across the slope with some uphill edge holds a traverse")
{
    Skier skier;  // traversing toward +X on a slope that falls toward +Y: the right edge is uphill
    skier.yaw = kQuarterTurn;
    skier.state.velocity = { 8.0F, 0.0F };
    float worst = 0.0F;
    for (int i = 0; i < 3 * 60; ++i) {
        skier.Step(SlopeDownY(25.0F), { .edge = 0.5F, .aim = 1.0F, .aimYaw = kQuarterTurn });
        worst = std::max(worst, AngleBetweenRadians(Heading(skier.state.velocity), kQuarterTurn));
    }
    CHECK(worst < 10.0F * kDegrees);
    CHECK(skier.Speed() > 4.0F);
}

TEST_CASE("steering harder than the skis can carve skids them and scrubs speed")
{
    Skier steered;  // edged right and steering hard right
    steered.state.velocity = { 0.0F, 15.0F };
    steered.Run(1.0F, kFlat, { .edge = 0.5F, .aim = 1.0F, .aimYaw = kQuarterTurn });

    Skier carved;  // the same edge, left to carve
    carved.state.velocity = { 0.0F, 15.0F };
    carved.Run(1.0F, kFlat, { .edge = 0.5F });

    CHECK(steered.last.skidding);
    CHECK(steered.Speed() < carved.Speed() - 1.0F);
    CHECK(Heading(steered.state.velocity) > 0.0F);
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

TEST_CASE("carving hard past across a steep slope never skis backwards")
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
}

TEST_CASE("stopping while facing uphill doesn't leave you skiing backwards")
{
    Skier skier;  // slowing to a stop, skis pointing mostly up a slope that falls toward +Y
    skier.yaw = 150.0F * kDegrees;
    skier.state.velocity = Scale2(Forward(skier.yaw), 2.0F);
    skier.Run(2.0F, SlopeDownY(20.0F), { .skid = true });
    CHECK(skier.last.parked);

    float worstSlip = 0.0F;
    for (int i = 0; i < 3 * 60; ++i) {
        skier.Step(SlopeDownY(20.0F));
        if (skier.Speed() > 1.0F) {
            worstSlip = std::max(worstSlip, std::fabs(skier.last.slipDegrees));
        }
    }
    CHECK(worstSlip < 95.0F);
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

TEST_CASE("in the air, momentum is kept apart from drag and the edges don't steer it")
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
    skier.Step(kFlat, { .aim = 1.0F, .aimYaw = -0.5F });
    CHECK(skier.yaw >= 0.0F && skier.yaw < 2.0F * std::numbers::pi_v<float>);
    CHECK(skier.yaw > 6.0F);
}


TEST_CASE("with nothing held, flat skis smearing sideways at speed swing round to the direction of travel")
{
    Skier skier;  // 35 degree slope falling toward +Y, skis across it, sliding straight down at 7 m/s
    skier.yaw = kQuarterTurn;
    skier.state.velocity = { 0.0F, 7.0F };
    skier.Run(2.5F, SlopeDownY(35.0F));
    CHECK(AngleBetweenRadians(skier.yaw, Heading(skier.state.velocity)) < 15.0F * kDegrees);
}

TEST_CASE("self-aligning leaves edged skis, skids and slow side-slips alone")
{
    Skier edged;  // traversing with a little edge, skis slipping 30 degrees
    edged.Edged(0.3F).yaw = kQuarterTurn;
    edged.state.velocity = Scale2(Forward(kQuarterTurn - 30.0F * kDegrees), 10.0F);
    edged.Step(kFlat, { .edge = 0.3F });
    Skier flat = edged;
    flat.state = {};
    flat.yaw = kQuarterTurn;
    flat.state.velocity = Scale2(Forward(kQuarterTurn - 30.0F * kDegrees), 10.0F);
    flat.Step(kFlat);
    CHECK(flat.yaw < edged.yaw);  // only the flat skis were pulled toward travel

    Skier leaf;  // falling leaf: slow, sideways, feathering on the uphill edge
    leaf.Edged(0.4F).yaw = kQuarterTurn;
    leaf.state.velocity = { 0.0F, 1.5F };
    leaf.Run(0.5F, SlopeDownY(25.0F), { .edge = 0.4F });
    CHECK_NEAR(leaf.yaw, kQuarterTurn, 0.01F);

    Skier slow;  // slow sideways slide with flat skis: side-slipping, not smearing
    slow.yaw = kQuarterTurn;
    slow.state.velocity = { 0.0F, 2.0F };
    slow.Step(SlopeDownY(25.0F));
    CHECK_NEAR(slow.yaw, kQuarterTurn, 1e-4F);
}

TEST_CASE("pushing firmly the way the skis point tucks, and tucking goes faster")
{
    Skier tucked;
    Skier standing;
    tucked.Run(20.0F, SlopeDownY(20.0F), { .aim = 1.0F, .aimYaw = 0.0F });
    standing.Run(20.0F, SlopeDownY(20.0F));
    CHECK_NEAR(tucked.last.tuck, 1.0F, 1e-3F);
    CHECK_NEAR(standing.last.tuck, 0.0F, 1e-6F);
    CHECK(tucked.Speed() > standing.Speed() * 1.2F);

    Skier turning;  // steering hard to the side isn't a tuck
    turning.state.velocity = { 0.0F, 10.0F };
    turning.Step(SlopeDownY(20.0F), { .aim = 1.0F, .aimYaw = kQuarterTurn });
    CHECK_NEAR(turning.last.tuck, 0.0F, 1e-6F);
}

TEST_CASE("a tuck holds through a steered turn and eases out when let go")
{
    Skier skier;
    skier.Run(3.0F, SlopeDownY(20.0F), { .aim = 1.0F, .aimYaw = 0.0F });
    CHECK_NEAR(skier.last.tuck, 1.0F, 1e-3F);

    // Steering 45 degrees off the skis, as W+D does: still tucked.
    for (int i = 0; i < 30; ++i) {
        skier.Step(SlopeDownY(20.0F), { .aim = 1.0F, .aimYaw = skier.yaw + 45.0F * kDegrees });
    }
    CHECK_NEAR(skier.last.tuck, 1.0F, 1e-3F);

    // Keys changing restart the aim's strength, but the push carries on: still tucked.
    skier.Step(SlopeDownY(20.0F), { .aim = 0.0F, .aimYaw = skier.yaw, .push = 1.0F });
    CHECK_NEAR(skier.last.tuck, 1.0F, 1e-3F);

    // Let go: one frame later only partly out of the tuck, fully out after the ease.
    skier.Step(SlopeDownY(20.0F));
    CHECK(skier.last.tuck > 0.9F);
    skier.Run(0.5F, SlopeDownY(20.0F));
    CHECK_NEAR(skier.last.tuck, 0.0F, 1e-6F);

    // Steering hard to the side leaves the tuck.
    skier.Run(3.0F, SlopeDownY(20.0F), { .aim = 1.0F, .aimYaw = skier.yaw });
    for (int i = 0; i < 60; ++i) {
        skier.Step(SlopeDownY(20.0F), { .aim = 1.0F, .aimYaw = skier.yaw + kQuarterTurn });
    }
    CHECK_NEAR(skier.last.tuck, 0.0F, 1e-6F);
}

TEST_CASE("landing along a slope is soft whatever the drop; meeting it head on is a fall")
{
    const Vec3 slope = SlopeDownY(30.0F);
    // Moving exactly along the slope: nothing into it.
    const Vec3 along{ 0.0F, 10.0F * std::cos(30.0F * kDegrees), -10.0F * std::sin(30.0F * kDegrees) };
    CHECK_NEAR(ImpactHeight(along, slope, 9.81F), 0.0F, 1e-4F);
    // Straight down onto flat ground: the height that gives that speed.
    CHECK_NEAR(ImpactHeight({ 0.0F, 0.0F, -14.0F }, kFlat, 9.81F), 14.0F * 14.0F / (2.0F * 9.81F), 1e-3F);
    // Moving away from the slope: nothing.
    CHECK_NEAR(ImpactHeight({ 0.0F, 0.0F, 5.0F }, kFlat, 9.81F), 0.0F, 1e-6F);
    // Dropping onto a 30 degree slope counts the part of the speed into it.
    const float into = 14.0F * std::cos(30.0F * kDegrees);
    CHECK_NEAR(ImpactHeight({ 0.0F, 0.0F, -14.0F }, slope, 9.81F), into * into / (2.0F * 9.81F), 1e-3F);
}

TEST_CASE("powder is slower, bites harder sideways and cushions landings")
{
    Skier packed;
    Skier powder;
    powder.settings.powder = true;
    packed.Run(30.0F, SlopeDownY(35.0F));
    powder.Run(30.0F, SlopeDownY(35.0F));
    CHECK(powder.Speed() < packed.Speed() * 0.85F);
    CHECK(powder.Speed() > packed.Speed() * 0.6F);

    Skier packedSmear;  // flat skis across the direction of travel on the flat
    packedSmear.yaw = kQuarterTurn;
    packedSmear.state.velocity = { 0.0F, 3.0F };
    Skier powderSmear = packedSmear;
    powderSmear.settings.powder = true;
    packedSmear.Run(0.5F, kFlat);
    powderSmear.Run(0.5F, kFlat);
    CHECK(powderSmear.Speed() < packedSmear.Speed() - 0.4F);

    const Vec3 drop{ 0.0F, 0.0F, -14.0F };
    CHECK_NEAR(LandingHeight(drop, kFlat, powder.settings),
        ImpactHeight(drop, kFlat, 9.81F) - powder.settings.powderLandingMetres, 1e-3F);
    CHECK_NEAR(LandingHeight(drop, kFlat, packed.settings), ImpactHeight(drop, kFlat, 9.81F), 1e-3F);
    CHECK(LandingHeight({ 0.0F, 0.0F, -5.0F }, kFlat, powder.settings) == 0.0F);
}

TEST_CASE("powder bites at speed on steep runs but leaves gentle cruising mostly alone")
{
    Skier packedGentle;
    Skier powderGentle;
    powderGentle.settings.powder = true;
    packedGentle.Run(40.0F, SlopeDownY(10.0F));
    powderGentle.Run(40.0F, SlopeDownY(10.0F));
    CHECK(powderGentle.Speed() > packedGentle.Speed() * 0.75F);
}

TEST_CASE("holding a clean carve pumps speed, a skid doesn't, and it can't build forever on the flat")
{
    Skier pumped;  // carving on the flat, slow enough for the edge to hold
    pumped.Edged(0.6F).state.velocity = { 0.0F, 8.0F };
    Skier flat = pumped;
    flat.settings.carvePump = 0.0F;
    pumped.Run(1.0F, kFlat, { .edge = 0.6F });
    flat.Run(1.0F, kFlat, { .edge = 0.6F });
    CHECK(pumped.last.pump > 0.0F);
    CHECK(pumped.Speed() > flat.Speed() + 0.3F);
    CHECK(pumped.Speed() < 8.0F);  // cancels drag, not snow friction

    Skier skidding;  // carving harder than the grip holds
    skidding.Edged(1.0F).state.velocity = { 0.0F, 25.0F };
    skidding.Step(kFlat, { .edge = 1.0F });
    CHECK_NEAR(skidding.last.pump, 0.0F, 1e-6F);

    Skier forever;  // full edge round and round on the flat: still slows down
    forever.Edged(1.0F).state.velocity = { 0.0F, 8.0F };
    forever.Run(20.0F, kFlat, { .edge = 1.0F });
    CHECK(forever.Speed() < 8.0F);
}

TEST_CASE("on the flat, alternating trigger squeezes with the stick forward skate you along")
{
    Skier skater;
    float side = 1.0F;
    int   strides = 0;
    for (int i = 0; i < 4 * 60; ++i) {
        const bool squeezing = (i % 30) < 10;  // a squeeze every half second, alternating sides
        if (i % 30 == 0) {
            side = -side;
        }
        skater.Step(kFlat, { .edge = squeezing ? side : 0.0F, .aim = 1.0F, .aimYaw = skater.yaw });
        strides += skater.last.skated ? 1 : 0;
    }
    CHECK(strides == 8);
    CHECK(skater.Speed() > 2.5F);
    CHECK(skater.Speed() < skater.settings.skateMaxSpeed);

    Skier sameSide;  // the same trigger every time pushes half as hard
    Skier first;
    for (auto* skier : { &sameSide, &first }) {
        skier->Step(kFlat, { .edge = 1.0F, .aim = 1.0F });
        skier->Run(0.2F, kFlat, { .aim = 1.0F });
    }
    sameSide.Step(kFlat, { .edge = 1.0F, .aim = 1.0F });
    first.Step(kFlat, { .edge = -1.0F, .aim = 1.0F });
    CHECK(sameSide.last.skated);
    CHECK(first.Speed() > sameSide.Speed());

    Skier noStick;  // squeezing without pushing forward is just edging
    noStick.Step(kFlat, { .edge = 1.0F });
    CHECK(!noStick.last.skated);

    Skier steep;  // not on a real slope
    steep.Step(SlopeDownY(25.0F), { .edge = 1.0F, .aim = 1.0F });
    CHECK(!steep.last.skated);
}

TEST_CASE("a stride's push builds over the push time rather than all at once")
{
    Skier skater;
    skater.Step(kFlat, { .edge = 1.0F, .aim = 1.0F });
    CHECK(skater.last.skated);
    CHECK_NEAR(skater.last.strideSide, 1.0F, 1e-6F);
    CHECK(skater.Speed() < 0.1F * skater.settings.skateImpulse);

    skater.Run(skater.settings.skatePushSeconds, kFlat, { .aim = 1.0F });
    CHECK(skater.Speed() > 0.8F * skater.settings.skateImpulse);
    CHECK(skater.Speed() < skater.settings.skateImpulse);
    CHECK(skater.last.strideSeconds > skater.settings.skatePushSeconds - 1e-3F);

    // Off the ground, the rest of the push is lost.
    Skier jumper;
    jumper.Step(kFlat, { .edge = 1.0F, .aim = 1.0F });
    jumper.Run(skater.settings.skatePushSeconds, kFlat, { .aim = 1.0F, .grounded = false });
    CHECK(jumper.Speed() < 0.1F * jumper.settings.skateImpulse);
}

TEST_CASE("a mouse-button skating stride edges lightly and lets go quickly, so the skis glide")
{
    Skier skater;
    skater.Step(kFlat, { .edge = 1.0F, .aim = 1.0F, .buttons = true });
    CHECK(skater.last.skated);
    skater.Run(0.3F, kFlat, { .edge = 1.0F, .aim = 1.0F, .buttons = true });
    CHECK(std::fabs(skater.state.edge) <= 0.25F + 1e-4F);
    skater.Run(0.05F, kFlat, { .aim = 1.0F, .buttons = true });
    CHECK_NEAR(skater.state.edge, 0.0F, 1e-4F);

    Skier carver;  // a click that isn't a stride (stick not pushed) still builds full edge
    carver.Run(0.6F, kFlat, { .edge = 1.0F, .buttons = true });
    CHECK(carver.state.edge > 0.9F);
}

TEST_CASE("out of stamina, edges only reach the tired fraction and strides don't start")
{
    Skier tired;
    tired.settings.tiredEdgeFraction = 0.3F;
    tired.Run(0.5F, kFlat, { .edge = 1.0F, .tired = true });
    CHECK_NEAR(tired.state.edge, 0.3F, 1e-4F);

    Skier skater;
    skater.Step(kFlat, { .edge = 1.0F, .aim = 1.0F, .tired = true });
    CHECK(!skater.last.skated);
    skater.Run(0.2F, kFlat, { .aim = 1.0F, .tired = true });
    skater.Step(kFlat, { .edge = -1.0F, .aim = 1.0F });
    CHECK(skater.last.skated);  // stamina back, the next squeeze strides
}

TEST_CASE("carving costs stamina with the square of the edge, only on the ground at speed")
{
    SkiSettings settings;
    settings.staminaPerCarveSecond = 10.0F;
    SkiStep full;
    full.velocity = { 0.0F, 10.0F };
    full.edgeDegrees = settings.maxEdgeDegrees;
    CHECK_NEAR(StaminaCost(full, true, 1.0F, settings), 10.0F, 1e-4F);

    SkiStep half = full;
    half.edgeDegrees = -settings.maxEdgeDegrees * 0.5F;
    CHECK_NEAR(StaminaCost(half, true, 1.0F, settings), 2.5F, 1e-4F);

    CHECK_NEAR(StaminaCost(full, false, 1.0F, settings), 0.0F, 1e-6F);  // in the air
    SkiStep slow = full;
    slow.velocity = { 0.0F, 0.5F };
    CHECK_NEAR(StaminaCost(slow, true, 1.0F, settings), 0.0F, 1e-6F);

    SkiStep striding = full;  // a skating stride's edge is paid for by the stride
    striding.strideSeconds = 0.3F;
    CHECK_NEAR(StaminaCost(striding, true, 1.0F, settings), 0.0F, 1e-6F);

    settings.staminaPerCarveSecond = 0.0F;
    CHECK_NEAR(StaminaCost(full, true, 1.0F, settings), 0.0F, 1e-6F);
}

TEST_CASE("each skating stride costs its stamina once")
{
    SkiSettings settings;
    settings.staminaPerStride = 6.0F;
    settings.staminaPerCarveSecond = 0.0F;
    SkiStep stride;
    stride.skated = true;
    stride.strideSeconds = 0.0F;
    CHECK_NEAR(StaminaCost(stride, true, 1.0F / 60.0F, settings), 6.0F, 1e-4F);
    stride.skated = false;
    stride.strideSeconds = 0.1F;
    CHECK_NEAR(StaminaCost(stride, true, 1.0F / 60.0F, settings), 0.0F, 1e-6F);
}