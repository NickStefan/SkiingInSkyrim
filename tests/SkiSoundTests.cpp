#include "Check.h"

#include "core/SkiSound.h"

using namespace SkiingInSkyrim::Core;

namespace
{
    struct Counts
    {
        int   turns{ 0 };
        int   skids{ 0 };
        float loudestSkid{ 0.0F };
    };

    // Seconds of skiing at 60 frames a second, the input changed each frame by step(frame, input).
    template <class Step>
    Counts Ski(SkiSoundState& state, SkiSoundInput input, float seconds, Step step, const SkiSoundSettings& settings = {})
    {
        Counts counts;
        input.deltaSeconds = 1.0F / 60.0F;
        for (int frame = 0; frame < static_cast<int>(seconds * 60.0F); ++frame) {
            step(frame, input);
            const auto mix = MixSkiSound(state, input, settings);
            for (std::size_t i = 0; i < mix.grainCount; ++i) {
                const auto& grain = mix.grains[i];
                if (grain.kind == SoundKind::Turn) {
                    ++counts.turns;
                } else if (grain.kind == SoundKind::Skid) {
                    ++counts.skids;
                    counts.loudestSkid = std::max(counts.loudestSkid, grain.volume);
                }
            }
        }
        return counts;
    }

    Counts Ski(SkiSoundState& state, SkiSoundInput input, float seconds, const SkiSoundSettings& settings = {})
    {
        return Ski(state, input, seconds, [](int, SkiSoundInput&) {}, settings);
    }
}

TEST_CASE("running straight is silent, however fast")
{
    SkiSoundState state;
    const auto counts = Ski(state, { .speed = 20.0F }, 5.0F);
    CHECK(counts.turns == 0);
    CHECK(counts.skids == 0);
}

TEST_CASE("each turn sounds once as it starts, and again at each change of direction")
{
    SkiSoundState state;
    // Linked turns: two seconds each way.
    const auto counts = Ski(state, { .speed = 10.0F }, 8.0F, [](int frame, SkiSoundInput& input) {
        input.turnAcceleration = (frame / 120) % 2 == 0 ? 5.0F : -5.0F;
    });
    CHECK(counts.turns == 4);
    CHECK(counts.skids == 0);
}

TEST_CASE("a held turn is one sound")
{
    SkiSoundState state;
    CHECK(Ski(state, { .speed = 10.0F, .turnAcceleration = 5.0F }, 5.0F).turns == 1);
}

TEST_CASE("slow or gentle turns are silent")
{
    SkiSoundState slow;
    CHECK(Ski(slow, { .speed = 2.0F, .turnAcceleration = 5.0F }, 2.0F).turns == 0);
    SkiSoundState gentle;
    CHECK(Ski(gentle, { .speed = 10.0F, .turnAcceleration = 1.0F }, 2.0F).turns == 0);
    SkiSoundState flying;
    CHECK(Ski(flying, { .grounded = false, .speed = 10.0F, .turnAcceleration = 5.0F }, 2.0F).turns == 0);
}

TEST_CASE("a skid sounds as it starts, keeps sounding while it lasts, louder the faster")
{
    SkiSoundState hard;
    const auto fast = Ski(hard, { .speed = 12.0F, .slipDegrees = 60.0F }, 3.0F);
    SkiSoundState soft;
    const auto slow = Ski(soft, { .speed = 2.0F, .slipDegrees = 60.0F }, 3.0F);
    CHECK(fast.skids >= 3);
    CHECK(fast.skids <= 6);
    CHECK(fast.loudestSkid > slow.loudestSkid);
    SkiSoundState carving;
    CHECK(Ski(carving, { .speed = 12.0F, .slipDegrees = 2.0F }, 3.0F).skids == 0);
}

TEST_CASE("a skidded turn is a skid, not a turn as well")
{
    SkiSoundState state;
    const auto counts = Ski(state, { .speed = 12.0F, .slipDegrees = 40.0F, .turnAcceleration = 6.0F }, 1.0F);
    CHECK(counts.turns == 0);
    CHECK(counts.skids >= 1);
}

TEST_CASE("a landing and a stride each play once, on their frame")
{
    SkiSoundState state;
    const auto mix = MixSkiSound(state, { .deltaSeconds = 1.0F / 60.0F, .strideStarted = true, .strideSide = 1.0F,
                                            .landingSpeed = 5.0F });
    int landings = 0;
    int strides = 0;
    for (std::size_t i = 0; i < mix.grainCount; ++i) {
        landings += mix.grains[i].kind == SoundKind::Landing ? 1 : 0;
        if (mix.grains[i].kind == SoundKind::Stride) {
            ++strides;
            CHECK(mix.grains[i].right);
        }
    }
    CHECK(landings == 1);
    CHECK(strides == 1);
    CHECK(MixSkiSound(state, { .deltaSeconds = 1.0F / 60.0F }).grainCount == 0);
}

TEST_CASE("a crash plays once, louder the more speed it took")
{
    const auto crash = [](float lost) {
        SkiSoundState state;
        SkiSoundSettings settings;
        settings.volumeVariation = 0.0F;
        const auto mix = MixSkiSound(state, { .deltaSeconds = 1.0F / 60.0F, .crashSpeed = lost }, settings);
        CHECK(mix.grainCount == 1);
        CHECK(mix.grains[0].kind == SoundKind::Crash);
        return mix.grains[0].volume;
    };
    CHECK(crash(10.0F) > crash(3.0F));
    SkiSoundState state;
    CHECK(MixSkiSound(state, { .deltaSeconds = 1.0F / 60.0F, .crashSpeed = 0.5F }).grainCount == 0);
}

TEST_CASE("every sound's pitch and volume vary a little, within the settings")
{
    SkiSoundState state;
    const SkiSoundSettings settings;
    float lowest = 2.0F;
    float highest = 0.0F;
    for (int i = 0; i < 200; ++i) {
        const auto mix = MixSkiSound(state, { .deltaSeconds = 0.0F, .strideStarted = true }, settings);
        CHECK(mix.grainCount == 1);
        const auto& grain = mix.grains[0];
        lowest = std::min(lowest, grain.pitch);
        highest = std::max(highest, grain.pitch);
        CHECK(grain.volume <= settings.strideVolume * settings.volume + 1e-4F);
        CHECK(grain.volume >= settings.strideVolume * settings.volume * (1.0F - settings.volumeVariation) - 1e-4F);
    }
    CHECK(lowest >= 1.0F - settings.pitchVariation - 1e-4F);
    CHECK(highest <= 1.0F + settings.pitchVariation + 1e-4F);
    CHECK(highest - lowest > settings.pitchVariation);
}

TEST_CASE("turns and skids share their volume between powder and packed snow by how deep the powder is")
{
    const auto half = ShareByPowder(0.8F, 0.5F);
    CHECK_NEAR(half.powder, 0.4F, 1e-4F);
    CHECK_NEAR(half.packed, 0.4F, 1e-4F);
    const auto deep = ShareByPowder(0.8F, 0.95F);
    CHECK(deep.powder == 0.8F);
    CHECK(deep.packed == 0.0F);
    const auto road = ShareByPowder(0.8F, 0.05F);
    CHECK(road.powder == 0.0F);
    CHECK(road.packed == 0.8F);

    SkiSoundState state;
    const auto mix = MixSkiSound(state, { .deltaSeconds = 1.0F / 60.0F, .speed = 12.0F, .slipDegrees = 60.0F,
                                            .powder = 0.7F });
    CHECK(mix.grainCount == 1);
    CHECK_NEAR(mix.grains[0].powder, 0.7F, 1e-4F);
}

TEST_CASE("sound off is silent")
{
    SkiSoundState state;
    SkiSoundSettings settings;
    settings.enabled = false;
    const auto counts = Ski(state, { .speed = 15.0F, .slipDegrees = 60.0F, .turnAcceleration = 6.0F }, 3.0F, settings);
    CHECK(counts.turns == 0);
    CHECK(counts.skids == 0);
}
