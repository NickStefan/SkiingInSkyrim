#pragma once

#include "core/CameraFollow.h"
#include "core/SkiPhysics.h"

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

    // A button, optionally held together with a modifier (e.g. LB + X).
    struct Hotkey
    {
        Button key;
        Button modifier;

        [[nodiscard]] bool Bound() const noexcept { return key.Bound(); }
        [[nodiscard]] bool operator==(const Hotkey&) const = default;
    };

    enum class Binding
    {
        Toggle,
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

    [[nodiscard]] std::span<const FloatField>  PhysicsFields() noexcept;
    [[nodiscard]] std::span<const BoolField>   PhysicsSwitches() noexcept;
    [[nodiscard]] std::span<const CameraField> CameraFields() noexcept;

    // Main thread, once game data and input devices exist.
    void Load();
    // Any thread.
    void Save();

    // All accessors are safe from any thread.
    [[nodiscard]] Core::SkiSettings Physics();
    void                            SetPhysics(const Core::SkiSettings& physics);

    [[nodiscard]] Core::CameraFollowSettings Camera();
    void                                     SetCamera(const Core::CameraFollowSettings& camera);

    [[nodiscard]] Hotkey      GetBinding(Binding binding);
    [[nodiscard]] std::string BindingName(Binding binding);  // "LB + X", or empty when unbound
    // Looks up display names through the game's input manager.
    void                      SetBinding(Binding binding, Hotkey hotkey);

    [[nodiscard]] bool ShowOverlay();
    void               SetShowOverlay(bool show);

    // Per-frame trace in the log while the character shakes vertically.
    [[nodiscard]] bool TraceShaking();
    void               SetTraceShaking(bool trace);

    // Steering on the right stick or mouse instead of the left stick or A/D; the left stick or A/D then look
    // around.
    [[nodiscard]] bool AimWithRightStick();
    void               SetAimWithRightStick(bool right);
    // Degrees the skis turn per count of mouse movement, steering with the mouse.
    inline constexpr float kMouseSteerMin = 0.01F;
    inline constexpr float kMouseSteerMax = 0.5F;
    [[nodiscard]] float MouseSteerSensitivity();
    void                SetMouseSteerSensitivity(float degreesPerCount);
}
