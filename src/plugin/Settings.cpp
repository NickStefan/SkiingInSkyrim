#include "plugin/Settings.h"

#include "core/Ini.h"

#include <fstream>
#include <sstream>

namespace SkiingInSkyrim::Settings
{
    namespace
    {
        constexpr auto kPath = "Data/SKSE/Plugins/SkiingInSkyrim.ini";

        constexpr std::uint32_t kLeftShift = 0x2A;
        constexpr std::uint32_t kGamepadX = 0x4000;

        using S = Core::SkiSettings;
        constexpr FloatField kPhysicsFields[] = {
            { "Gravity", "Gravity", "m/s^2. Scales how hard slopes pull you.", &S::gravity, 2.0F, 20.0F, "%.2f" },
            { "SnowFriction", "Snow friction", "Friction coefficient. Slopes gentler than atan(this) won't start a slide.",
                &S::snowFriction, 0.0F, 0.3F, "%.3f" },
            { "DragPerSpeedSquared", "Air drag", "Deceleration per (m/s)^2. Lower means a higher top speed.",
                &S::dragPerSpeedSquared, 0.0F, 0.05F, "%.4f" },
            { "MaxSpeed", "Max speed", "m/s. Hard cap.", &S::maxSpeed, 5.0F, 60.0F, "%.1f" },
            { "SidecutRadius", "Sidecut radius", "Metres. A carving ski follows an arc of sidecut * cos(edge angle); "
                                                 "smaller carves tighter.",
                &S::sidecutRadius, 5.0F, 40.0F, "%.1f" },
            { "MaxEdgeDegrees", "Max edge angle", "Degrees of edge at a full trigger or a fully held mouse button.",
                &S::maxEdgeDegrees, 10.0F, 70.0F, "%.0f" },
            { "EdgeEngageDegrees", "Edge engage angle", "Degrees. Below this the ski is only partly engaged and "
                                                        "carves a wider arc.",
                &S::edgeEngageDegrees, 0.0F, 30.0F, "%.0f" },
            { "FlatGrip", "Flat grip", "Sideways friction of flat skis: how fast a smear or side-slip slows you.",
                &S::flatGrip, 0.0F, 0.5F, "%.3f" },
            { "EdgeGripFactor", "Edge grip", "Sideways grip added by edging, times tan(edge angle). Limits how tight you can "
                                             "carve at speed and how hard a hockey stop brakes.",
                &S::edgeGrip, 0.1F, 2.0F, "%.2f" },
            { "TriggerEdgeRamp", "Trigger edge ramp", "Full edge range per second when edging with the triggers; "
                                                      "smooths the trigger.",
                &S::triggerEdgeRamp, 1.0F, 30.0F, "%.1f" },
            { "MouseEdgeRamp", "Mouse edge ramp", "Full edge range per second while a mouse button is held.",
                &S::buttonEdgeRamp, 0.2F, 10.0F, "%.2f" },
            { "MouseEdgeRelease", "Mouse edge release", "Full edge range per second after a mouse button is released; "
                                                        "slow keeps the edge from a tap.",
                &S::buttonEdgeRelease, 0.1F, 10.0F, "%.2f" },
            { "PivotMaxDegrees", "Skid twist", "Degrees the skis twist (pivot) when the skid button is held fully.",
                &S::pivotMaxDegrees, 20.0F, 120.0F, "%.0f" },
            { "PivotBuildSeconds", "Skid twist time", "Seconds of holding skid to reach the full twist; a shorter hold "
                                                       "gives part of it.",
                &S::pivotBuildSeconds, 0.05F, 2.0F, "%.2f" },
            { "PivotReturnSeconds", "Skid return time", "Seconds for the skis to untwist after releasing skid.",
                &S::pivotReturnSeconds, 0.05F, 2.0F, "%.2f" },
            { "ParkSpeed", "Stop speed", "m/s. Slowing below this while holding skid counts as stopped: you're held "
                                         "there, and the skis stay where they are when you let go.",
                &S::parkSpeed, 0.0F, 2.0F, "%.2f" },
            { "SteerDegreesPerSecond", "Steering rate", "Degrees per second the skis turn toward where the stick (or "
                                                        "W/A/S/D) aims, at a full push.",
                &S::steerDegreesPerSecond, 10.0F, 360.0F, "%.0f" },
            { "AimDeadzone", "Stick deadzone", "Fraction of the stick's travel, from the centre, that does nothing.",
                &S::aimDeadzone, 0.0F, 0.6F, "%.2f" },
            { "TuckDragReduction", "Tuck", "Fraction of air drag cut by tucking (pushing firmly the way the skis "
                                           "point). Higher means faster in a tuck; 0 is off.",
                &S::tuckDragReduction, 0.0F, 0.8F, "%.2f" },
            { "SelfAlignDegreesPerSecond", "Self-align rate", "Degrees per second flat skis smearing sideways at speed "
                                                              "swing round to where you're going, with the stick centred "
                                                              "and nothing held. Slow side-slips are left alone; 0 is off.",
                &S::selfAlignDegreesPerSecond, 0.0F, 180.0F, "%.0f" },
            { "PowderDrag", "Powder drag", "Deceleration per m/s of speed from ploughing through deep snow, when "
                                           "powder is on. Higher means slower; 0.15 takes about a fifth off top speed "
                                           "on steep runs.",
                &S::powderDrag, 0.0F, 0.5F, "%.2f" },
            { "PowderSideGrip", "Powder side grip", "Flat grip is multiplied by this in powder: skids, stops and "
                                                    "steered turns bite harder.",
                &S::powderSideGrip, 1.0F, 4.0F, "%.2f" },
            { "PowderLanding", "Powder landing", "Fraction of a landing's impact the powder absorbs.",
                &S::powderLanding, 0.0F, 1.0F, "%.2f" },
            { "GroundHugSeconds", "Ground hug time", "Seconds off the ground over a crest (not a jump) during which "
                                                     "you're held to the slope; longer absorbs bigger crests, 0 is off.",
                &S::groundHugSeconds, 0.0F, 1.0F, "%.2f" },
            { "GroundHugGravity", "Ground hug pull", "m/s^2 of extra downward pull while held to the slope.",
                &S::groundHugGravity, 0.0F, 60.0F, "%.0f" },
            { "GroundHugMaxDropSpeed", "Ground hug max drop", "m/s. The pull can't take you down faster than this "
                                                              "beyond following the slope, so you don't slam back onto it.",
                &S::groundHugMaxDropSpeed, 0.0F, 10.0F, "%.1f" },
            { "BlockedRatio", "Blocked ratio", "If the engine delivers less than this fraction of our speed, give way.",
                &S::blockedRatio, 0.3F, 0.95F, "%.2f" },
            { "BlockedBlend", "Blocked blend", "Per-frame blend toward the delivered speed while blocked.", &S::blockedBlend,
                0.05F, 1.0F, "%.2f" },
            { "BlockedMinSpeed", "Blocked min speed", "m/s. Below this, blocking is not checked.", &S::blockedMinSpeed, 0.0F,
                3.0F, "%.2f" },
        };

        constexpr BoolField kPhysicsSwitches[] = {
            { "Powder", "Powder", "Deep snow everywhere: slower, skids and stops bite harder, landings are softer "
                                  "(see the Powder sliders).",
                &S::powder },
        };

        using C = Core::CameraFollowSettings;
        constexpr CameraField kCameraFields[] = {
            { "FollowSeconds", "Camera follow lag", "Seconds. How lazily the camera swings round to your direction of "
                                                    "travel once it's outside the lazy zone.",
                &C::followSeconds, 0.05F, 2.0F, "%.2f" },
            { "TravelSmoothingSeconds", "Travel smoothing", "Seconds the direction of travel is averaged over first, so "
                                                            "quick back-and-forth carves cancel out.",
                &C::travelSmoothingSeconds, 0.0F, 2.0F, "%.2f" },
            { "LazyZoneDegrees", "Lazy zone", "Degrees. Within this of the camera, the direction of travel only pulls "
                                              "it round slowly.",
                &C::lazyZoneDegrees, 0.0F, 45.0F, "%.0f" },
            { "RecenterSeconds", "Lazy zone recenter", "Seconds. How slowly the camera recenters within the lazy zone.",
                &C::recenterSeconds, 0.2F, 6.0F, "%.2f" },
            { "LookHoldSeconds", "Look-around hold", "Seconds the camera stays where you looked before easing back.",
                &C::lookHoldSeconds, 0.0F, 5.0F, "%.2f" },
            { "LookReturnSeconds", "Look-around return", "Seconds. How lazily the camera eases back after looking around.",
                &C::lookReturnSeconds, 0.05F, 3.0F, "%.2f" },
        };

        struct BindingInfo
        {
            Binding     binding;
            const char* key;  // INI key prefix
            const char* help;
            Hotkey      defaults;
        };

        const BindingInfo kBindings[] = {
            { Binding::Toggle, "ToggleKey", "Toggle skiing mode", {} },
            { Binding::SkidKeyboard, "SkidKey", "Skid (keyboard/mouse)", { { RE::INPUT_DEVICE::kKeyboard, kLeftShift }, {} } },
            { Binding::SkidGamepad, "SkidButton", "Skid (gamepad)", { { RE::INPUT_DEVICE::kGamepad, kGamepadX }, {} } },
        };
        constexpr std::size_t kBindingCount = std::size(kBindings);

        bool Accepts(Binding binding, RE::INPUT_DEVICE device)
        {
            switch (binding) {
            case Binding::SkidKeyboard:
                return device == RE::INPUT_DEVICE::kKeyboard || device == RE::INPUT_DEVICE::kMouse;
            case Binding::SkidGamepad:
                return device == RE::INPUT_DEVICE::kGamepad;
            default:
                return device >= RE::INPUT_DEVICE::kKeyboard && device <= RE::INPUT_DEVICE::kGamepad;
            }
        }

        struct State
        {
            Core::SkiSettings                       physics;
            Core::CameraFollowSettings              camera;
            std::array<Hotkey, kBindingCount>       bindings;
            std::array<std::string, kBindingCount>  bindingNames;
            bool                                    showOverlay{ false };
            bool                                    aimWithRightStick{ false };
        };

        std::mutex g_lock;
        State      g_state;

        std::string ButtonName(Button button)
        {
            RE::BSFixedString name;
            auto* manager = RE::BSInputDeviceManager::GetSingleton();
            if (manager && manager->GetButtonNameFromID(button.device, static_cast<std::int32_t>(button.id), name) &&
                !name.empty()) {
                return name.c_str();
            }
            return fmt::format("device {} button {}", static_cast<int>(button.device), button.id);
        }

        std::string HotkeyName(const Hotkey& hotkey)
        {
            if (!hotkey.Bound()) {
                return {};
            }
            return hotkey.modifier.Bound() ? ButtonName(hotkey.modifier) + " + " + ButtonName(hotkey.key) :
                                             ButtonName(hotkey.key);
        }

        std::optional<Button> ReadButton(const Core::Ini& ini, Binding binding, const std::string& key)
        {
            const auto device = ini.GetInt("Controls", key + "Device");
            const auto id = ini.GetInt("Controls", key);
            if (!device) {
                return std::nullopt;
            }
            if (*device < 0) {
                return Button{};
            }
            if (!id || *id < 0 || !Accepts(binding, static_cast<RE::INPUT_DEVICE>(*device))) {
                return std::nullopt;
            }
            return Button{ static_cast<RE::INPUT_DEVICE>(*device), static_cast<std::uint32_t>(*id) };
        }

        void WriteButton(std::string& text, const std::string& key, Button button)
        {
            text += fmt::format("{}Device = {}\n{} = {}\n", key, button.Bound() ? static_cast<int>(button.device) : -1,
                key, button.id);
        }
    }

    std::span<const FloatField>  PhysicsFields() noexcept { return kPhysicsFields; }
    std::span<const BoolField>   PhysicsSwitches() noexcept { return kPhysicsSwitches; }
    std::span<const CameraField> CameraFields() noexcept { return kCameraFields; }

    void Load()
    {
        State loaded;
        for (std::size_t i = 0; i < kBindingCount; ++i) {
            loaded.bindings[i] = kBindings[i].defaults;
        }

        std::ifstream file(kPath, std::ios::binary);
        if (file) {
            std::stringstream text;
            text << file.rdbuf();
            const auto ini = Core::Ini::Parse(text.str());

            for (std::size_t i = 0; i < kBindingCount; ++i) {
                const auto& info = kBindings[i];
                if (const auto key = ReadButton(ini, info.binding, info.key)) {
                    loaded.bindings[i] = { *key, {} };
                    if (const auto modifier = ReadButton(ini, info.binding, std::string(info.key) + "Modifier")) {
                        loaded.bindings[i].modifier = *modifier;
                    }
                }
            }
            if (const auto value = ini.GetInt("Controls", "AimWithRightStick")) {
                loaded.aimWithRightStick = *value != 0;
            }
            for (const auto& field : kPhysicsFields) {
                if (const auto value = ini.GetFloat("Physics", field.key)) {
                    loaded.physics.*field.member = std::clamp(*value, field.min, field.max);
                }
            }
            for (const auto& field : PhysicsSwitches()) {
                if (const auto value = ini.GetInt("Physics", field.key)) {
                    loaded.physics.*field.member = *value != 0;
                }
            }
            if (const auto value = ini.GetInt("Camera", "FollowTravel")) {
                loaded.camera.enabled = *value != 0;
            }
            for (const auto& field : kCameraFields) {
                if (const auto value = ini.GetFloat("Camera", field.key)) {
                    loaded.camera.*field.member = std::clamp(*value, field.min, field.max);
                }
            }
            if (const auto value = ini.GetInt("Display", "ShowOverlay")) {
                loaded.showOverlay = *value != 0;
            }
            spdlog::info("settings loaded from {}", kPath);
        } else {
            spdlog::info("no {}, using defaults", kPath);
        }
        for (std::size_t i = 0; i < kBindingCount; ++i) {
            loaded.bindingNames[i] = HotkeyName(loaded.bindings[i]);
        }

        std::scoped_lock lock(g_lock);
        g_state = std::move(loaded);
    }

    void Save()
    {
        State state;
        {
            std::scoped_lock lock(g_lock);
            state = g_state;
        }
        std::string text;
        text += "; Skiing in Skyrim settings. Written by the in-game menu; edit there or here.\n\n";
        text += "[Controls]\n";
        text += "; Buttons: device (0 keyboard, 1 mouse, 2 gamepad, -1 unbound) and button id. A modifier, if bound,\n";
        text += "; must be held when the button is pressed.\n";
        for (std::size_t i = 0; i < kBindingCount; ++i) {
            text += fmt::format("; {}\n", kBindings[i].help);
            WriteButton(text, kBindings[i].key, state.bindings[i].key);
            WriteButton(text, std::string(kBindings[i].key) + "Modifier", state.bindings[i].modifier);
        }
        text += "; Steer with the right stick instead of the left (1 on, 0 off). The left stick then looks around.\n";
        text += fmt::format("AimWithRightStick = {}\n", state.aimWithRightStick ? 1 : 0);
        text += "\n[Display]\n";
        text += "; Debug overlay with the skis, velocity, fall line and edge angle (1 on, 0 off).\n";
        text += fmt::format("ShowOverlay = {}\n\n", state.showOverlay ? 1 : 0);
        text += "[Camera]\n";
        text += "; Third-person camera follows your direction of travel while skiing (1 on, 0 off).\n";
        text += fmt::format("FollowTravel = {}\n", state.camera.enabled ? 1 : 0);
        for (const auto& field : kCameraFields) {
            text += fmt::format("; {}\n{} = {}\n", field.help, field.key, state.camera.*field.member);
        }
        text += "\n[Physics]\n";
        for (const auto& field : kPhysicsFields) {
            text += fmt::format("; {}\n{} = {}\n", field.help, field.key, state.physics.*field.member);
        }
        for (const auto& field : PhysicsSwitches()) {
            text += fmt::format("; {} (1 on, 0 off)\n{} = {}\n", field.help, field.key, state.physics.*field.member ? 1 : 0);
        }

        std::ofstream file(kPath, std::ios::binary | std::ios::trunc);
        if (!file || !(file << text)) {
            spdlog::warn("could not write {}", kPath);
        }
    }

    Core::SkiSettings Physics()
    {
        std::scoped_lock lock(g_lock);
        return g_state.physics;
    }

    void SetPhysics(const Core::SkiSettings& physics)
    {
        std::scoped_lock lock(g_lock);
        g_state.physics = physics;
    }

    Core::CameraFollowSettings Camera()
    {
        std::scoped_lock lock(g_lock);
        return g_state.camera;
    }

    void SetCamera(const Core::CameraFollowSettings& camera)
    {
        std::scoped_lock lock(g_lock);
        g_state.camera = camera;
    }

    Hotkey GetBinding(Binding binding)
    {
        std::scoped_lock lock(g_lock);
        return g_state.bindings[static_cast<std::size_t>(binding)];
    }

    std::string BindingName(Binding binding)
    {
        std::scoped_lock lock(g_lock);
        return g_state.bindingNames[static_cast<std::size_t>(binding)];
    }

    void SetBinding(Binding binding, Hotkey hotkey)
    {
        if (!Accepts(binding, hotkey.key.device)) {
            hotkey = {};
        }
        auto name = HotkeyName(hotkey);
        spdlog::info("{} set to {}", kBindings[static_cast<std::size_t>(binding)].help, name.empty() ? "unbound" : name);
        std::scoped_lock lock(g_lock);
        g_state.bindings[static_cast<std::size_t>(binding)] = hotkey;
        g_state.bindingNames[static_cast<std::size_t>(binding)] = std::move(name);
    }

    bool ShowOverlay()
    {
        std::scoped_lock lock(g_lock);
        return g_state.showOverlay;
    }

    void SetShowOverlay(bool show)
    {
        std::scoped_lock lock(g_lock);
        g_state.showOverlay = show;
    }

    bool AimWithRightStick()
    {
        std::scoped_lock lock(g_lock);
        return g_state.aimWithRightStick;
    }

    void SetAimWithRightStick(bool right)
    {
        std::scoped_lock lock(g_lock);
        g_state.aimWithRightStick = right;
    }
}
