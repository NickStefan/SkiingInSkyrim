#include "plugin/Settings.h"

#include "core/Ini.h"

#include <fstream>
#include <sstream>

namespace SkiingInSkyrim::Settings
{
    namespace
    {
        constexpr auto kPath = "Data/SKSE/Plugins/SkiingInSkyrim.ini";

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
            { "MaxEdgeDegrees", "Max edge angle", "Degrees of edge at full stick or a fully held key.", &S::maxEdgeDegrees,
                10.0F, 70.0F, "%.0f" },
            { "EdgeEngageDegrees", "Edge engage angle", "Degrees. Below this the ski is only partly engaged and "
                                                        "carves a wider arc.",
                &S::edgeEngageDegrees, 0.0F, 30.0F, "%.0f" },
            { "FlatGrip", "Flat grip", "Sideways friction of flat skis: how fast a smear or side-slip slows you.",
                &S::flatGrip, 0.0F, 0.5F, "%.3f" },
            { "EdgeGripFactor", "Edge grip", "Sideways grip added by edging, times tan(edge angle). Limits how tight you can "
                                       "carve at speed and how hard a hockey stop brakes.",
                &S::edgeGrip, 0.1F, 2.0F, "%.2f" },
            { "StickEdgeRamp", "Stick edge ramp", "Full edge range per second when using the stick.", &S::stickEdgeRamp,
                1.0F, 30.0F, "%.1f" },
            { "KeyEdgeRamp", "Key edge ramp", "Full edge range per second while A or D is held.", &S::keyEdgeRamp, 0.2F,
                10.0F, "%.2f" },
            { "KeyEdgeRelease", "Key edge release", "Full edge range per second after A or D is released; slow keeps "
                                                    "the edge from a tap.",
                &S::keyEdgeRelease, 0.1F, 10.0F, "%.2f" },
            { "PivotDegreesPerSecond", "Pivot rate", "Degrees per second the skis twist at full pivot on flat skis.",
                &S::pivotDegreesPerSecond, 20.0F, 400.0F, "%.0f" },
            { "PivotEdgedFactor", "Pivot on edge", "Fraction of the pivot rate left at full edge.", &S::pivotEdgedFactor,
                0.0F, 1.0F, "%.2f" },
            { "PivotRamp", "Pivot ramp", "Full pivot range per second; how quickly pivot input eases in and out.",
                &S::pivotRamp, 1.0F, 30.0F, "%.1f" },
            { "BlockedRatio", "Blocked ratio", "If the engine delivers less than this fraction of our speed, give way.",
                &S::blockedRatio, 0.3F, 0.95F, "%.2f" },
            { "BlockedBlend", "Blocked blend", "Per-frame blend toward the delivered speed while blocked.", &S::blockedBlend,
                0.05F, 1.0F, "%.2f" },
            { "BlockedMinSpeed", "Blocked min speed", "m/s. Below this, blocking is not checked.", &S::blockedMinSpeed, 0.0F,
                3.0F, "%.2f" },
        };

        struct State
        {
            Core::SkiSettings physics;
            Hotkey            toggleKey;
            std::string       toggleKeyName;
        };

        std::mutex g_lock;
        State      g_state;

        std::string KeyName(Hotkey key)
        {
            if (!key.Bound()) {
                return {};
            }
            RE::BSFixedString name;
            auto* manager = RE::BSInputDeviceManager::GetSingleton();
            if (manager && manager->GetButtonNameFromID(key.device, static_cast<std::int32_t>(key.id), name) && !name.empty()) {
                return name.c_str();
            }
            return fmt::format("device {} button {}", static_cast<int>(key.device), key.id);
        }
    }

    std::span<const FloatField> PhysicsFields() noexcept { return kPhysicsFields; }

    void Load()
    {
        State loaded;
        std::ifstream file(kPath, std::ios::binary);
        if (file) {
            std::stringstream text;
            text << file.rdbuf();
            const auto ini = Core::Ini::Parse(text.str());

            const auto device = ini.GetInt("Controls", "ToggleKeyDevice");
            const auto id = ini.GetInt("Controls", "ToggleKey");
            if (device && id && *device >= RE::INPUT_DEVICE::kKeyboard && *device <= RE::INPUT_DEVICE::kGamepad && *id >= 0) {
                loaded.toggleKey = { static_cast<RE::INPUT_DEVICE>(*device), static_cast<std::uint32_t>(*id) };
            }
            for (const auto& field : kPhysicsFields) {
                if (const auto value = ini.GetFloat("Physics", field.key)) {
                    loaded.physics.*field.member = std::clamp(*value, field.min, field.max);
                }
            }
            spdlog::info("settings loaded from {}", kPath);
        } else {
            spdlog::info("no {}, using defaults", kPath);
        }
        loaded.toggleKeyName = KeyName(loaded.toggleKey);

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
        text += "; Toggle key: device (0 keyboard, 1 mouse, 2 gamepad, -1 unbound) and button id.\n";
        text += fmt::format("ToggleKeyDevice = {}\n", static_cast<int>(state.toggleKey.device));
        text += fmt::format("ToggleKey = {}\n\n", state.toggleKey.id);
        text += "[Physics]\n";
        for (const auto& field : kPhysicsFields) {
            text += fmt::format("; {}\n{} = {}\n", field.help, field.key, state.physics.*field.member);
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

    Hotkey ToggleKey()
    {
        std::scoped_lock lock(g_lock);
        return g_state.toggleKey;
    }

    std::string ToggleKeyName()
    {
        std::scoped_lock lock(g_lock);
        return g_state.toggleKeyName;
    }

    void SetToggleKey(Hotkey key)
    {
        auto name = KeyName(key);
        {
            std::scoped_lock lock(g_lock);
            g_state.toggleKey = key;
            g_state.toggleKeyName = std::move(name);
        }
        spdlog::info("toggle key set to {}", key.Bound() ? fmt::format("device {} id {}", static_cast<int>(key.device), key.id) : "unbound");
    }
}
