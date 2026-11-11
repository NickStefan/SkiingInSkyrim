#pragma once

#include "core/CameraFollow.h"
#include "core/SkiPhysics.h"
#include "core/SkiSound.h"
#include "core/Stance.h"

namespace SkiingInSkyrim::Settings
{
    struct Button
    {
        RE::INPUT_DEVICE device{ RE::INPUT_DEVICE::kNone };
        std::uint32_t    id{ 0 };

        [[nodiscard]] bool Bound() const noexcept { return device != RE::INPUT_DEVICE::kNone; }
        [[nodiscard]] bool Is(RE::INPUT_DEVICE otherDevice, std::uint32_t otherId) const noexcept
        {
            return Bound() && device == otherDevice && id == otherId;
        }
        [[nodiscard]] bool operator==(const Button&) const = default;
    };

    enum class Binding
    {
        SkidKeyboard,  // keyboard or mouse
        SkidGamepad,
    };

    // One tunable value; drives both the INI file and the menu slider.
    template <class Owner>
    struct FloatFieldOf
    {
        const char* key;
        const char* label;
        const char* help;
        float Owner::*member;
        float       min;
        float       max;
        const char* format;
    };

    template <class Owner>
    struct BoolFieldOf
    {
        const char* key;
        const char* label;
        const char* help;
        bool Owner::*member;
    };

    using FloatField = FloatFieldOf<Core::SkiSettings>;
    using BoolField = BoolFieldOf<Core::SkiSettings>;
    using CameraField = FloatFieldOf<Core::CameraFollowSettings>;
    using StanceField = FloatFieldOf<Core::StanceSettings>;
    using StanceSwitch = BoolFieldOf<Core::StanceSettings>;
    using SoundField = FloatFieldOf<Core::SkiSoundSettings>;
    using SoundSwitch = BoolFieldOf<Core::SkiSoundSettings>;

    // Menu pages the physics settings are shown on.
    enum class Page
    {
        General,
        Controls,
        Speed,
        Stamina,
        Impacts,
    };

    // Physics settings shown together under a heading (none if empty), and saved in one INI section.
    struct PhysicsGroup
    {
        Page                        page;
        const char*                 section;
        const char*                 title;
        std::span<const BoolField>  switches;
        std::span<const FloatField> fields;
        const char*                 intro{ nullptr };  // shown under the heading
    };

    [[nodiscard]] std::span<const PhysicsGroup> PhysicsGroups() noexcept;
    [[nodiscard]] std::span<const CameraField>  CameraFields() noexcept;
    [[nodiscard]] std::span<const StanceField>  StanceFields() noexcept;
    [[nodiscard]] std::span<const StanceSwitch> StanceSwitches() noexcept;
    [[nodiscard]] std::span<const SoundField>   SoundFields() noexcept;
    [[nodiscard]] std::span<const SoundSwitch>  SoundSwitches() noexcept;

    // Main thread, once game data and input devices exist.
    void Load();
    // Any thread.
    void Save();

    // All accessors are safe from any thread.
    [[nodiscard]] Core::SkiSettings Physics();
    void                            SetPhysics(const Core::SkiSettings& physics);

    [[nodiscard]] Core::CameraFollowSettings Camera();
    void                                     SetCamera(const Core::CameraFollowSettings& camera);

    // The skier's body pose while skiing.
    [[nodiscard]] bool                 StanceEnabled();
    void                               SetStanceEnabled(bool enabled);
    [[nodiscard]] Core::StanceSettings Stance();
    void                               SetStance(const Core::StanceSettings& stance);

    [[nodiscard]] Core::SkiSoundSettings Sound();
    void                                 SetSound(const Core::SkiSoundSettings& sound);

    [[nodiscard]] Button      GetBinding(Binding binding);
    [[nodiscard]] std::string BindingName(Binding binding);  // "X", or empty when unbound
    // Looks up display names through the game's input manager.
    void                      SetBinding(Binding binding, Button button);

    // Master switch: off, wearing skis does nothing.
    [[nodiscard]] bool ModEnabled();
    void               SetModEnabled(bool enabled);

    [[nodiscard]] bool ShowOverlay();
    void               SetShowOverlay(bool show);

    // Per-frame trace in the log while the character shakes vertically.
    [[nodiscard]] bool TraceShaking();
    void               SetTraceShaking(bool trace);
    // Telemetry, surface, air, impact and tumble camera lines in the log; off, it only logs what changes.
    [[nodiscard]] bool DetailedLog();
    void               SetDetailedLog(bool detailed);

    // Whether looking around (mouse, right stick) moves where W/A/S/D or the left stick aim: on, they aim
    // relative to where the camera looks; off, relative to where the chase camera points by itself.
    [[nodiscard]] bool LookSteersKeyboardMouse();
    void               SetLookSteersKeyboardMouse(bool steers);
    [[nodiscard]] bool LookSteersGamepad();
    void               SetLookSteersGamepad(bool steers);
}
