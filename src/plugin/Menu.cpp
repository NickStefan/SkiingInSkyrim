#include "plugin/Menu.h"

#include "plugin/Input.h"
#include "plugin/Movement.h"
#include "plugin/Overlay.h"
#include "plugin/Settings.h"
#include "plugin/SkiingState.h"
#include "plugin/Tdm.h"

#include <SKSEMenuFramework.h>

namespace SkiingInSkyrim::Menu
{
    namespace
    {
        const ImGuiMCP::ImVec4 kGood{ 0.45F, 0.85F, 0.45F, 1.0F };
        const ImGuiMCP::ImVec4 kWarn{ 0.95F, 0.75F, 0.3F, 1.0F };

        void Line(const std::string& text) { ImGuiMCP::TextUnformatted(text.c_str()); }
        void Colored(const ImGuiMCP::ImVec4& color, const std::string& text) { ImGuiMCP::TextColored(color, "%s", text.c_str()); }

        void RenderTdm()
        {
            const auto tdm = Tdm::GetInfo();
            switch (tdm.connection) {
            case Tdm::Connection::NotInstalled:
                ImGuiMCP::TextDisabled("%s", "True Directional Movement: not installed");
                return;
            case Tdm::Connection::NoCompatibleApi:
                Colored(kWarn, "True Directional Movement: installed, but no API with yaw control (needs API v2+)");
                Line("It may turn the character while skiing.");
                return;
            case Tdm::Connection::Connected:
                Colored(kGood, fmt::format("True Directional Movement: connected (API v{})", tdm.apiVersion));
                break;
            }

            if (tdm.yawControl) {
                Colored(kGood, "Facing control: held by Skiing in Skyrim");
            } else if (tdm.yawDenied) {
                Colored(kWarn, fmt::format("Facing control: denied ({}), retrying", tdm.lastResult));
            } else if (IsSkiingEnabled()) {
                Line("Facing control: taken over when you return to the game");
            } else {
                Line("Facing control: TDM (taken over while skiing)");
            }
        }

        void RenderMovementChain()
        {
            const auto chain = Movement::GetChainInfo();
            if (chain.iceIsSlickLoaded) {
                Line("Ice is Slick: detected");
            } else if (chain.installed) {
                ImGuiMCP::TextDisabled("%s", "Ice is Slick: not installed");
            }

            if (!chain.installed) {
                Line("Movement hook: installed when you enter the game");
            } else if (chain.outermost) {
                Colored(kGood, fmt::format("Movement hook: applied last (after {})", chain.runsBeforeUs));
            } else {
                Colored(kWarn, fmt::format("Movement hook: {} runs after Skiing in Skyrim and may override it",
                                   chain.runsAfterUs));
            }
        }

        void RenderToggleKey()
        {
            const auto name = Settings::ToggleKeyName();
            Line(fmt::format("Toggle key: {}", name.empty() ? "unbound" : name));
            ImGuiMCP::SameLine();
            if (Input::IsCapturing()) {
                ImGuiMCP::TextColored(kWarn, "%s", "press a key, mouse button or gamepad button (Esc cancels)");
            } else {
                if (ImGuiMCP::Button("Set")) {
                    Input::BeginCapture();
                }
                if (Settings::ToggleKey().Bound()) {
                    ImGuiMCP::SameLine();
                    if (ImGuiMCP::Button("Clear")) {
                        Settings::SetToggleKey({});
                        Settings::Save();
                    }
                }
            }
        }

        void __stdcall RenderSettings()
        {
            bool enabled = IsSkiingEnabled();
            if (ImGuiMCP::Checkbox("Skiing mode", &enabled)) {
                SetSkiingEnabled(enabled, "menu");
            }
            RenderToggleKey();
            bool overlay = Settings::ShowOverlay();
            if (ImGuiMCP::Checkbox("Show debug overlay", &overlay)) {
                Settings::SetShowOverlay(overlay);
                Settings::Save();
            }
            ImGuiMCP::TextWrapped("%s", "Edge with the triggers or the mouse buttons (left/right edge): the skis carve "
                                        "an arc that tightens with more edge. Attacks and blocking are off while "
                                        "skiing. Pivot (twist the skis) with the left stick or A/D; they stay where "
                                        "you leave them.");
            ImGuiMCP::TextWrapped("%s", "To slow down, pivot the skis across your direction of travel and edge hard: "
                                        "a hockey stop. Flat skis only smear. W and S do nothing yet. Telemetry is "
                                        "written to the log while enabled.");

            ImGuiMCP::SeparatorText("Compatibility");
            RenderTdm();
            RenderMovementChain();
        }

        void __stdcall RenderTuning()
        {
            ImGuiMCP::TextWrapped("%s", "Changes apply immediately and are saved to Data/SKSE/Plugins/SkiingInSkyrim.ini "
                                        "when you release a slider. Ctrl+click a slider to type a value.");
            auto physics = Settings::Physics();
            bool changed = false;
            bool finished = false;
            for (const auto& field : Settings::PhysicsSwitches()) {
                if (ImGuiMCP::Checkbox(field.label, &(physics.*field.member))) {
                    changed = true;
                    finished = true;
                }
                if (ImGuiMCP::IsItemHovered()) {
                    ImGuiMCP::SetTooltip("%s", field.help);
                }
            }
            for (const auto& field : Settings::PhysicsFields()) {
                changed |= ImGuiMCP::SliderFloat(field.label, &(physics.*field.member), field.min, field.max, field.format);
                finished |= ImGuiMCP::IsItemDeactivatedAfterEdit();
                if (ImGuiMCP::IsItemHovered()) {
                    ImGuiMCP::SetTooltip("%s", field.help);
                }
            }
            if (changed) {
                Settings::SetPhysics(physics);
            }
            ImGuiMCP::Spacing();
            if (ImGuiMCP::Button("Reset to defaults")) {
                Settings::SetPhysics({});
                finished = true;
            }
            if (finished) {
                Settings::Save();
            }
        }

        bool __stdcall OnFrameworkInput(RE::InputEvent* event) { return Input::OnMenuFrameworkInput(event); }
    }

    bool Register()
    {
        if (!SKSEMenuFramework::IsInstalled()) {
            return false;
        }
        SKSEMenuFramework::SetSection("Skiing in Skyrim");
        SKSEMenuFramework::AddSectionItem("Settings", RenderSettings);
        SKSEMenuFramework::AddSectionItem("Tuning", RenderTuning);
        SKSEMenuFramework::AddInputEvent(OnFrameworkInput);
        SKSEMenuFramework::AddHudElement(Overlay::Render);
        return true;
    }
}
