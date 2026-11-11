#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace SkiingInSkyrim::Core
{
    // Skiing in good snow is nearly silent, so the skis only sound when something happens: a turn, a skid, a
    // skating stride, a landing, a crash.
    struct SkiSoundSettings
    {
        bool  enabled{ true };
        bool  silenceFootsteps{ true };  // the walk/run animation's footsteps under the stance (the plugin's)
        float volume{ 0.8F };            // times every sound
        float turnVolume{ 0.6F };
        float skidVolume{ 0.6F };
        float strideVolume{ 0.5F };
        float landingVolume{ 0.7F };     // landings and crashes
        float turnMinSpeed{ 3.0F };      // m/s, slower turns are silent
        float turnFullSpeed{ 15.0F };    // m/s, turns at full volume
        float turnAcceleration{ 2.5F };  // m/s^2 the path curves at that counts as a turn
        float skidStartSpeed{ 1.0F };    // m/s sideways
        float skidFullSpeed{ 6.0F };     // m/s sideways, skids at full volume
        float skidRepeatSeconds{ 0.7F }; // a long skid plays another sound this often
        float pitchVariation{ 0.08F };   // each sound's pitch, up to this fraction either way
        float volumeVariation{ 0.25F };  // each sound's volume, up to this fraction quieter
    };

    struct SkiSoundInput
    {
        float deltaSeconds{ 0.0F };
        bool  grounded{ true };
        float speed{ 0.0F };             // m/s along the ground
        float slipDegrees{ 0.0F };       // the skis' angle to the way they're going
        float turnAcceleration{ 0.0F };  // m/s^2 the path curves at, smoothed, positive right
        float powder{ 0.0F };            // 0..1, how deep in powder: turns and skids blend powder and packed sounds
        bool  strideStarted{ false };    // a skating stride started this frame
        float strideSide{ 0.0F };        // its leg, +1 right, -1 left
        float landingSpeed{ 0.0F };      // m/s into the slope, on the frame the skis touch down; else 0
        float crashSpeed{ 0.0F };        // m/s lost into a tree, rock or wall, on the frame it's judged; else 0
    };

    enum class SoundKind
    {
        Turn,
        Skid,
        Stride,
        Landing,
        Crash,
    };

    // One sound to play now.
    struct SoundGrain
    {
        SoundKind kind{ SoundKind::Turn };
        float     volume{ 0.0F };  // 0..1
        float     pitch{ 1.0F };   // frequency multiplier
        bool      right{ false };  // which ski or way (variants alternate)
        float     powder{ 0.0F };  // turns and skids: the powder sound's share of the volume, the rest packed snow's
    };

    // A turn's or skid's volume shared between the powder and packed-snow sounds: none to one side under
    // kSmallestShare, so they don't both play for a barely audible part.
    struct SoundShares
    {
        float powder{ 0.0F };
        float packed{ 0.0F };
    };
    inline constexpr float kSmallestShare = 0.15F;
    [[nodiscard]] SoundShares ShareByPowder(float volume, float powder) noexcept;

    struct SkiSoundState
    {
        float         turnSide{ 0.0F };     // which way the path is curving, 0 straight
        float         sinceTurn{ 10.0F };   // seconds
        bool          skidding{ false };
        float         untilSkid{ 0.0F };    // seconds to the next sound of a long skid
        bool          nextRight{ false };
        std::uint32_t seed{ 0x9E3779B9U };
    };

    struct SkiSoundMix
    {
        std::array<SoundGrain, 4> grains{};
        std::size_t               grainCount{ 0 };
    };

    // The sounds to start this frame. Each gets its own small pitch and volume change, so repeats don't sound
    // the same.
    [[nodiscard]] SkiSoundMix MixSkiSound(SkiSoundState& state, const SkiSoundInput& input,
        const SkiSoundSettings& settings = {}) noexcept;
}
