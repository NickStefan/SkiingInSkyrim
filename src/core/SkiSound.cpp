#include "core/SkiSound.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace SkiingInSkyrim::Core
{
    namespace
    {
        constexpr float kTurnGapSeconds = 0.3F;  // turns closer together than this are one
        constexpr float kSkidStopFraction = 0.7F;  // a skid ends below this share of its start speed
        constexpr float kLandingStartSpeed = 1.0F;  // m/s into the slope
        constexpr float kLandingFullSpeed = 8.0F;
        constexpr float kCrashStartSpeed = 1.5F;  // m/s lost
        constexpr float kCrashFullSpeed = 12.0F;

        float Random(std::uint32_t& seed) noexcept
        {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            return static_cast<float>(seed >> 8) / static_cast<float>(1U << 24);
        }

        float Clamp01(float value) noexcept { return std::clamp(value, 0.0F, 1.0F); }

        float SmoothStep(float from, float to, float value) noexcept
        {
            const float t = Clamp01((value - from) / std::max(to - from, 1e-3F));
            return t * t * (3.0F - 2.0F * t);
        }

        void Add(SkiSoundMix& mix, SkiSoundState& state, SoundKind kind, float volume, bool right,
            const SkiSoundSettings& settings) noexcept
        {
            if (mix.grainCount >= mix.grains.size() || volume <= 0.0F) {
                return;
            }
            const float pitch = 1.0F + settings.pitchVariation * (2.0F * Random(state.seed) - 1.0F);
            const float level = volume * (1.0F - settings.volumeVariation * Random(state.seed));
            mix.grains[mix.grainCount++] = { kind, Clamp01(level * settings.volume), pitch, right };
        }
    }

    SoundShares ShareByPowder(float volume, float powder) noexcept
    {
        float share = Clamp01(powder);
        if (share < kSmallestShare) {
            share = 0.0F;
        } else if (share > 1.0F - kSmallestShare) {
            share = 1.0F;
        }
        return { volume * share, volume * (1.0F - share) };
    }

    SkiSoundMix MixSkiSound(SkiSoundState& state, const SkiSoundInput& input, const SkiSoundSettings& settings) noexcept
    {
        SkiSoundMix mix;
        const float delta = std::max(0.0F, input.deltaSeconds);
        state.sinceTurn += delta;
        if (!settings.enabled) {
            state.skidding = false;
            state.turnSide = 0.0F;
            return mix;
        }

        if (input.landingSpeed >= kLandingStartSpeed) {
            const float hard = SmoothStep(kLandingStartSpeed, kLandingFullSpeed, input.landingSpeed);
            Add(mix, state, SoundKind::Landing, (0.3F + 0.7F * hard) * settings.landingVolume, state.nextRight, settings);
        }
        if (input.crashSpeed >= kCrashStartSpeed) {
            const float hard = SmoothStep(kCrashStartSpeed, kCrashFullSpeed, input.crashSpeed);
            Add(mix, state, SoundKind::Crash, (0.4F + 0.6F * hard) * settings.landingVolume, state.nextRight, settings);
        }
        if (input.strideStarted) {
            Add(mix, state, SoundKind::Stride, settings.strideVolume, input.strideSide > 0.0F, settings);
        }

        const float slip = std::min(std::fabs(input.slipDegrees) * std::numbers::pi_v<float> / 180.0F,
            std::numbers::pi_v<float> / 2.0F);
        const float sideways = input.grounded ? input.speed * std::sin(slip) : 0.0F;
        if (!state.skidding && sideways >= settings.skidStartSpeed) {
            state.skidding = true;
            state.untilSkid = 0.0F;
        } else if (state.skidding && sideways < settings.skidStartSpeed * kSkidStopFraction) {
            state.skidding = false;
        }
        if (state.skidding) {
            state.untilSkid -= delta;
            if (state.untilSkid <= 0.0F) {
                const float hard = SmoothStep(settings.skidStartSpeed, settings.skidFullSpeed, sideways);
                Add(mix, state, SoundKind::Skid, (0.3F + 0.7F * hard) * settings.skidVolume, state.nextRight, settings);
                state.nextRight = !state.nextRight;
                state.untilSkid = settings.skidRepeatSeconds * (0.85F + 0.3F * Random(state.seed));
            }
        }

        // A turn sounds as the path starts curving one way: entering a turn, and at each change of direction.
        const float curve = input.grounded ? input.turnAcceleration : 0.0F;
        float       side = state.turnSide;
        if (std::fabs(curve) >= settings.turnAcceleration) {
            side = curve > 0.0F ? 1.0F : -1.0F;
        } else if (std::fabs(curve) < settings.turnAcceleration * 0.5F) {
            side = 0.0F;
        }
        if (side != 0.0F && side != state.turnSide && !state.skidding && input.speed >= settings.turnMinSpeed &&
            state.sinceTurn >= kTurnGapSeconds) {
            const float fast = SmoothStep(settings.turnMinSpeed, settings.turnFullSpeed, input.speed);
            const float hard = SmoothStep(settings.turnAcceleration, settings.turnAcceleration * 3.0F, std::fabs(curve));
            Add(mix, state, SoundKind::Turn, (0.3F + 0.7F * fast) * (0.6F + 0.4F * hard) * settings.turnVolume,
                side > 0.0F, settings);
            state.sinceTurn = 0.0F;
        }
        state.turnSide = side;
        for (std::size_t i = 0; i < mix.grainCount; ++i) {
            mix.grains[i].powder = Clamp01(input.powder);
        }
        return mix;
    }
}
