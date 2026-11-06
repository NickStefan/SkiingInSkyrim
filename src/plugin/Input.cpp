#include "plugin/Input.h"

#include "plugin/Settings.h"
#include "plugin/Skiing.h"
#include "plugin/SkiingState.h"

namespace SkiingInSkyrim::Input
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        // Held keys repeat an event every frame, so a key that has gone quiet this long was
        // released while we weren't listening (menus, focus loss).
        constexpr auto kStaleKey = std::chrono::milliseconds(200);

        enum Key : std::size_t
        {
            kForward,
            kBack,
            kLeft,
            kRight,
            kMovementKeyCount
        };

        enum PivotButton : std::size_t
        {
            kMouseLeft,
            kMouseRight,
            kTriggerLeft,
            kTriggerRight,
            kPivotButtonCount
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
            std::array<KeyState, kMovementKeyCount> keys;
            std::array<KeyState, kPivotButtonCount> pivots;
            float                            stickX{ 0.0F };
            float                            stickY{ 0.0F };
            std::optional<Clock::time_point> lastStick;
        };

        // Input events and PlayerCharacter::Update both run on the main thread.
        State g_state;

        // Set from the menu's render thread.
        std::atomic<bool> g_capturing{ false };

        constexpr std::uint32_t kEscape = 0x01;
        constexpr std::uint32_t kLeftMouse = 0;
        constexpr std::uint32_t kRightMouse = 1;
        constexpr std::uint32_t kLeftTrigger = 0x0009;
        constexpr std::uint32_t kRightTrigger = 0x000A;

        std::optional<PivotButton> PivotButtonOf(const RE::ButtonEvent& button)
        {
            const auto id = button.GetIDCode();
            switch (button.GetDevice()) {
            case RE::INPUT_DEVICE::kMouse:
                return id == kLeftMouse ? std::optional{ kMouseLeft } : id == kRightMouse ? std::optional{ kMouseRight } : std::nullopt;
            case RE::INPUT_DEVICE::kGamepad:
                return id == kLeftTrigger ? std::optional{ kTriggerLeft } : id == kRightTrigger ? std::optional{ kTriggerRight } : std::nullopt;
            default:
                return std::nullopt;
            }
        }

        bool CanToggle()
        {
            auto* ui = RE::UI::GetSingleton();
            return ui && !ui->GameIsPaused() && !ui->IsMenuOpen(RE::Console::MENU_NAME);
        }

        void OnButton(const RE::ButtonEvent& button)
        {
            if (button.IsDown() && !g_capturing && Settings::ToggleKey().Matches(button) && CanToggle()) {
                SetSkiingEnabled(!IsSkiingEnabled(), "hotkey");
            }
            if (const auto pivot = PivotButtonOf(button)) {
                g_state.pivots[*pivot].Set(button);
                return;
            }
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
                g_state.keys[*key].Set(button);
            }
        }

        void OnThumbstick(const RE::ThumbstickEvent& stick)
        {
            if (!stick.IsLeft()) {
                return;
            }
            g_state.stickX = stick.xValue;
            g_state.stickY = stick.yValue;
            g_state.lastStick = Clock::now();
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

        bool Held(const KeyState& key, Clock::time_point now)
        {
            return key.pressed && key.lastEvent && now - *key.lastEvent < kStaleKey;
        }

        float HeldValue(const KeyState& key, Clock::time_point now) { return Held(key, now) ? key.value : 0.0F; }

        struct AttackBlockCanProcess
        {
            static bool thunk(RE::AttackBlockHandler* handler, RE::InputEvent* event)
            {
                if (Skiing::GetDebug().active) {
                    return false;
                }
                return func(handler, event);
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };
    }

    void Install()
    {
        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_AttackBlockHandler[0] };
        AttackBlockCanProcess::func = vtable.write_vfunc(0x1, AttackBlockCanProcess::thunk);
        spdlog::info("hooked AttackBlockHandler::CanProcess");
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
        result.forward = Held(g_state.keys[kForward], now);
        result.back = Held(g_state.keys[kBack], now);
        result.left = Held(g_state.keys[kLeft], now);
        result.right = Held(g_state.keys[kRight], now);
        result.stickX = g_state.stickX;
        result.stickY = g_state.stickY;
        result.stickAge = Age(g_state.lastStick, now);
        result.pivotLeft = std::max(HeldValue(g_state.pivots[kMouseLeft], now), HeldValue(g_state.pivots[kTriggerLeft], now));
        result.pivotRight = std::max(HeldValue(g_state.pivots[kMouseRight], now), HeldValue(g_state.pivots[kTriggerRight], now));

        std::optional<Clock::time_point> latest;
        for (const auto& key : g_state.keys) {
            if (key.lastEvent && (!latest || *key.lastEvent > *latest)) {
                latest = key.lastEvent;
            }
        }
        result.keyAge = Age(latest, now);
        return result;
    }

    void BeginCapture() noexcept { g_capturing = true; }
    void CancelCapture() noexcept { g_capturing = false; }
    bool IsCapturing() noexcept { return g_capturing; }

    bool OnMenuFrameworkInput(RE::InputEvent* event)
    {
        if (!g_capturing || !event) {
            return false;
        }
        const auto* button = event->AsButtonEvent();
        if (!button || !button->IsDown()) {
            return false;
        }
        const auto device = button->GetDevice();
        const auto id = button->GetIDCode();
        if (device == RE::INPUT_DEVICE::kKeyboard && id == kEscape) {
            g_capturing = false;
            return true;
        }
        if ((device == RE::INPUT_DEVICE::kMouse && id == kLeftMouse) || device > RE::INPUT_DEVICE::kGamepad) {
            return false;
        }
        Settings::SetToggleKey({ device, id });
        Settings::Save();
        g_capturing = false;
        return true;
    }
}
