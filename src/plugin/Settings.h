#pragma once

#include "core/SkiPhysics.h"

namespace SkiingInSkyrim::Settings
{
    struct Hotkey
    {
        RE::INPUT_DEVICE device{ RE::INPUT_DEVICE::kNone };
        std::uint32_t    id{ 0 };

        [[nodiscard]] bool Bound() const noexcept { return device != RE::INPUT_DEVICE::kNone; }
        [[nodiscard]] bool Matches(const RE::ButtonEvent& button) const
        {
            return Bound() && button.GetDevice() == device && button.GetIDCode() == id;
        }
    };

    // One tunable physics parameter; drives both the INI file and the menu slider.
    struct FloatField
    {
        const char* key;
        const char* label;
        const char* help;
        float Core::SkiSettings::*member;
        float       min;
        float       max;
        const char* format;
    };

    struct BoolField
    {
        const char* key;
        const char* label;
        const char* help;
        bool Core::SkiSettings::*member;
    };

    [[nodiscard]] std::span<const FloatField> PhysicsFields() noexcept;
    [[nodiscard]] std::span<const BoolField>  PhysicsSwitches() noexcept;

    // Main thread, once game data and input devices exist.
    void Load();
    // Any thread.
    void Save();

    // All accessors are safe from any thread.
    [[nodiscard]] Core::SkiSettings Physics();
    void                            SetPhysics(const Core::SkiSettings& physics);

    [[nodiscard]] Hotkey      ToggleKey();
    [[nodiscard]] std::string ToggleKeyName();
    // Looks up the key's display name through the game's input manager when bound.
    void                      SetToggleKey(Hotkey key);

    [[nodiscard]] bool ShowOverlay();
    void               SetShowOverlay(bool show);
}
