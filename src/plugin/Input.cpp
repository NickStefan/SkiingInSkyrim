#include "plugin/Input.h"

#include "plugin/Settings.h"
#include "plugin/Skiing.h"

#include <set>

namespace SkiingInSkyrim::Input
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        using Settings::Binding;
        using Settings::Button;

        // Held buttons repeat an event every frame, so a button that has gone quiet this long was
        // released while we weren't listening (menus, focus loss).
        constexpr auto kStaleKey = std::chrono::milliseconds(200);

        constexpr std::uint32_t kEscape = 0x01;
        constexpr std::uint32_t kLeftMouse = 0;
        constexpr std::uint32_t kRightMouse = 1;
        constexpr std::uint32_t kLeftTrigger = 0x0009;
        constexpr std::uint32_t kRightTrigger = 0x000A;

        enum Key : std::size_t
        {
            kForward,
            kBack,
            kLeft,
            kRight,
            kMovementKeyCount
        };

        struct KeyState
        {
            bool                             pressed{ false };
            float                            value{ 0.0F };
            std::optional<Clock::time_point> lastEvent;

            void Set(const RE::ButtonEvent& button)
            {
                pressed = button.IsPressed();
                value = pressed ? std::clamp(button.Value(), 0.0F, 1.0F) : 0.0F;
                lastEvent = Clock::now();
            }
        };

        struct State
        {
            std::array<KeyState, kMovementKeyCount>  movement;
            std::map<std::uint64_t, KeyState>        buttons;  // every button seen, by device and id
            float                                    stickX{ 0.0F };
            float                                    stickY{ 0.0F };
            std::optional<Clock::time_point>         lastStick;
            float                                    rightStickX{ 0.0F };
            float                                    rightStickY{ 0.0F };
            std::optional<Clock::time_point>         lastRightStick;
            int                                      mouseX{ 0 };
        };

        // Input events, the menu framework's input callback and PlayerCharacter::Update all run on the
        // main thread.
        State g_state;

        // Binding being captured, or -1. Set from the menu's render thread.
        std::atomic<int>           g_capturing{ -1 };
        std::atomic<std::uint32_t> g_captureGeneration{ 0 };

        // Buttons pressed since the current capture began (main thread). Only these can be bound, so the
        // release of the button that clicked "Set" isn't taken as the new binding.
        std::set<std::uint64_t> g_capturePressed;
        std::uint32_t           g_capturePressedGeneration{ 0 };

        std::uint64_t ButtonKey(RE::INPUT_DEVICE device, std::uint32_t id)
        {
            return (static_cast<std::uint64_t>(device) << 32) | id;
        }

        bool Held(const KeyState& key, Clock::time_point now)
        {
            return key.pressed && key.lastEvent && now - *key.lastEvent < kStaleKey;
        }

        const KeyState* Find(RE::INPUT_DEVICE device, std::uint32_t id)
        {
            const auto it = g_state.buttons.find(ButtonKey(device, id));
            return it == g_state.buttons.end() ? nullptr : &it->second;
        }

        bool Held(Button button, Clock::time_point now)
        {
            const auto* state = button.Bound() ? Find(button.device, button.id) : nullptr;
            return state && Held(*state, now);
        }

        float HeldValue(RE::INPUT_DEVICE device, std::uint32_t id, Clock::time_point now)
        {
            const auto* state = Find(device, id);
            return state && Held(*state, now) ? state->value : 0.0F;
        }

        bool ModifierHeld(const Settings::Hotkey& hotkey, Clock::time_point now)
        {
            return !hotkey.modifier.Bound() || Held(hotkey.modifier, now);
        }

        bool HotkeyHeld(const Settings::Hotkey& hotkey, Clock::time_point now)
        {
            return hotkey.Bound() && Held(hotkey.key, now) && ModifierHeld(hotkey, now);
        }

        void Track(const RE::ButtonEvent& button)
        {
            g_state.buttons[ButtonKey(button.GetDevice(), button.GetIDCode())].Set(button);
        }

        void OnButton(const RE::ButtonEvent& button)
        {
            Track(button);
            if (button.GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
                return;
            }
            const auto* events = RE::UserEvents::GetSingleton();
            if (!events) {
                return;
            }
            const auto& name = button.QUserEvent();
            std::optional<Key> key;
            if (name == events->forward) {
                key = kForward;
            } else if (name == events->back) {
                key = kBack;
            } else if (name == events->strafeLeft) {
                key = kLeft;
            } else if (name == events->strafeRight) {
                key = kRight;
            }
            if (key) {
                g_state.movement[*key].Set(button);
            }
        }

        void OnThumbstick(const RE::ThumbstickEvent& stick)
        {
            if (stick.IsLeft()) {
                g_state.stickX = stick.xValue;
                g_state.stickY = stick.yValue;
                g_state.lastStick = Clock::now();
            } else if (stick.IsRight()) {
                g_state.rightStickX = stick.xValue;
                g_state.rightStickY = stick.yValue;
                g_state.lastRightStick = Clock::now();
            }
        }

        class Sink final : public RE::BSTEventSink<RE::InputEvent*>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
            {
                for (auto* event = a_event ? *a_event : nullptr; event; event = event->next) {
                    switch (event->GetEventType()) {
                    case RE::INPUT_EVENT_TYPE::kButton:
                        OnButton(*static_cast<RE::ButtonEvent*>(event));
                        break;
                    case RE::INPUT_EVENT_TYPE::kThumbstick:
                        OnThumbstick(*static_cast<RE::ThumbstickEvent*>(event));
                        break;
                    case RE::INPUT_EVENT_TYPE::kMouseMove:
                        g_state.mouseX += static_cast<RE::MouseMoveEvent*>(event)->mouseInputX;
                        break;
                    default:
                        break;
                    }
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        Sink g_sink;

        float Age(const std::optional<Clock::time_point>& when, Clock::time_point now)
        {
            return when ? std::chrono::duration<float>(now - *when).count() : -1.0F;
        }

        // Attack/block (triggers, mouse buttons) and ready weapon (gamepad X) are skiing controls.
        template <class Handler>
        struct SuppressWhileSkiing
        {
            static bool thunk(Handler* handler, RE::InputEvent* event)
            {
                if (Skiing::GetDebug().active) {
                    return false;
                }
                return func(handler, event);
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        // Steering with the right stick or mouse, they mustn't also turn the camera: the stick's aim is
        // relative to the camera, so the two would chase each other round.
        struct SuppressSteeringLook
        {
            static bool thunk(RE::LookHandler* handler, RE::InputEvent* event)
            {
                if (Skiing::GetDebug().active && Settings::AimWithRightStick() && event) {
                    const auto type = event->GetEventType();
                    if (type == RE::INPUT_EVENT_TYPE::kMouseMove ||
                        (type == RE::INPUT_EVENT_TYPE::kThumbstick && static_cast<RE::ThumbstickEvent*>(event)->IsRight())) {
                        return false;
                    }
                }
                return func(handler, event);
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        template <class Handler>
        void Suppress(REL::VariantID vtableId, const char* name)
        {
            REL::Relocation<std::uintptr_t> vtable{ vtableId };
            SuppressWhileSkiing<Handler>::func = vtable.write_vfunc(0x1, SuppressWhileSkiing<Handler>::thunk);
            spdlog::info("hooked {}::CanProcess", name);
        }
    }

    void Install()
    {
        Suppress<RE::AttackBlockHandler>(RE::VTABLE_AttackBlockHandler[0], "AttackBlockHandler");
        Suppress<RE::ReadyWeaponHandler>(RE::VTABLE_ReadyWeaponHandler[0], "ReadyWeaponHandler");
        REL::Relocation<std::uintptr_t> look{ RE::VTABLE_LookHandler[0] };
        SuppressSteeringLook::func = look.write_vfunc(0x1, SuppressSteeringLook::thunk);
        spdlog::info("hooked LookHandler::CanProcess");
    }

    bool Register()
    {
        auto* manager = RE::BSInputDeviceManager::GetSingleton();
        if (!manager) {
            return false;
        }
        manager->AddEventSink<RE::InputEvent*>(&g_sink);
        return true;
    }

    Snapshot Get()
    {
        const auto now = Clock::now();
        Snapshot result;
        result.forward = Held(g_state.movement[kForward], now);
        result.back = Held(g_state.movement[kBack], now);
        result.left = Held(g_state.movement[kLeft], now);
        result.right = Held(g_state.movement[kRight], now);
        result.stickX = g_state.stickX;
        result.stickY = g_state.stickY;
        result.stickAge = Age(g_state.lastStick, now);
        result.rightStickX = g_state.rightStickX;
        result.rightStickY = g_state.rightStickY;
        result.rightStickAge = Age(g_state.lastRightStick, now);
        result.triggerLeft = HeldValue(RE::INPUT_DEVICE::kGamepad, kLeftTrigger, now);
        result.triggerRight = HeldValue(RE::INPUT_DEVICE::kGamepad, kRightTrigger, now);
        result.mouseLeft = Held(Button{ RE::INPUT_DEVICE::kMouse, kLeftMouse }, now);
        result.mouseRight = Held(Button{ RE::INPUT_DEVICE::kMouse, kRightMouse }, now);
        result.skid = HotkeyHeld(Settings::GetBinding(Binding::SkidKeyboard), now) ||
                       HotkeyHeld(Settings::GetBinding(Binding::SkidGamepad), now);

        std::optional<Clock::time_point> latest;
        for (const auto& key : g_state.movement) {
            if (key.lastEvent && (!latest || *key.lastEvent > *latest)) {
                latest = key.lastEvent;
            }
        }
        result.keyAge = Age(latest, now);
        return result;
    }

    int TakeMouseX() { return std::exchange(g_state.mouseX, 0); }

    void BeginCapture(Settings::Binding binding) noexcept
    {
        ++g_captureGeneration;
        g_capturing = static_cast<int>(binding);
    }
    void CancelCapture() noexcept { g_capturing = -1; }

    std::optional<Settings::Binding> Capturing() noexcept
    {
        const int binding = g_capturing;
        return binding < 0 ? std::nullopt : std::optional{ static_cast<Binding>(binding) };
    }

    bool OnMenuFrameworkInput(RE::InputEvent* event)
    {
        const auto* button = event ? event->AsButtonEvent() : nullptr;
        if (!button) {
            return false;
        }
        Track(*button);
        const auto binding = Capturing();
        if (!binding) {
            return false;
        }
        const auto device = button->GetDevice();
        const auto id = button->GetIDCode();
        if (device == RE::INPUT_DEVICE::kKeyboard && id == kEscape) {
            if (button->IsDown()) {
                CancelCapture();
            }
            return true;
        }
        if (device == RE::INPUT_DEVICE::kMouse && id == kLeftMouse) {
            return false;
        }
        const bool gamepad = device == RE::INPUT_DEVICE::kGamepad;
        const bool keyboardOrMouse = device == RE::INPUT_DEVICE::kKeyboard || device == RE::INPUT_DEVICE::kMouse;
        if ((*binding == Binding::SkidGamepad && !gamepad) || (*binding == Binding::SkidKeyboard && !keyboardOrMouse) ||
            (!gamepad && !keyboardOrMouse)) {
            return false;
        }
        if (const auto generation = g_captureGeneration.load(); generation != g_capturePressedGeneration) {
            g_capturePressed.clear();
            g_capturePressedGeneration = generation;
        }
        const auto pressed = ButtonKey(device, id);
        if (button->IsDown()) {
            g_capturePressed.insert(pressed);
        }
        // Bind on release, so a modifier can be pressed first and held.
        if (!button->IsUp()) {
            return true;
        }
        if (!g_capturePressed.contains(pressed)) {
            return false;
        }

        Settings::SetBinding(*binding, { { device, id }, {} });
        Settings::Save();
        CancelCapture();
        return true;
    }
}
