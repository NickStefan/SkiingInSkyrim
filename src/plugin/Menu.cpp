#include "plugin/Menu.h"

#include "plugin/Camera.h"
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

        void RenderCamera()
        {
            const auto camera = Camera::GetInfo();
            Line(fmt::format("Chase camera: {}", camera.status));
            if (camera.smoothCam) {
                Line("SmoothCam: detected. It smooths the camera's position; if the chase camera stutters or "
                     "fights it, turn one of them off.");
            } else {
                ImGuiMCP::TextDisabled("%s", "SmoothCam: not installed");
            }
        }

        void RenderBinding(Settings::Binding binding, const char* label, const char* prompt)
        {
            ImGuiMCP::PushID(static_cast<int>(binding));
            const auto name = Settings::BindingName(binding);
            Line(fmt::format("{}: {}", label, name.empty() ? "unbound" : name));
            ImGuiMCP::SameLine();
            if (Input::Capturing() == binding) {
                ImGuiMCP::TextColored(kWarn, "%s", prompt);
            } else {
                if (ImGuiMCP::Button("Set")) {
                    Input::BeginCapture(binding);
                }
                if (Settings::GetBinding(binding).Bound()) {
                    ImGuiMCP::SameLine();
                    if (ImGuiMCP::Button("Clear")) {
                        Settings::SetBinding(binding, {});
                        Settings::Save();
                    }
                }
            }
            ImGuiMCP::PopID();
        }

        void RenderBindings()
        {
            using Settings::Binding;
            RenderBinding(Binding::Toggle, "Toggle skiing",
                "hold a modifier if you want one, then press and release a button (Esc cancels)");
            RenderBinding(Binding::SkidKeyboard, "Skid (keyboard/mouse)", "press a key or mouse button (Esc cancels)");
            RenderBinding(Binding::SkidGamepad, "Skid (gamepad)", "press a gamepad button (Esc cancels)");

            const auto toggle = Settings::GetBinding(Binding::Toggle);
            for (const auto skid : { Binding::SkidKeyboard, Binding::SkidGamepad }) {
                const auto binding = Settings::GetBinding(skid);
                if (toggle.Bound() && binding.Bound() && toggle.key == binding.key && !toggle.modifier.Bound()) {
                    Colored(kWarn, "The toggle is the same button as skid: pressing skid will turn skiing off. "
                                   "Give the toggle a modifier (hold e.g. LB while setting it) or another button.");
                }
            }
        }

        void __stdcall RenderSettings()
        {
            bool enabled = IsSkiingEnabled();
            if (ImGuiMCP::Checkbox("Skiing mode", &enabled)) {
                SetSkiingEnabled(enabled, "menu");
            }
            RenderBindings();
            bool overlay = Settings::ShowOverlay();
            if (ImGuiMCP::Checkbox("Show debug overlay", &overlay)) {
                Settings::SetShowOverlay(overlay);
                Settings::Save();
            }
            auto camera = Settings::Camera();
            if (ImGuiMCP::Checkbox("Camera follows direction of travel", &camera.enabled)) {
                Settings::SetCamera(camera);
                Settings::Save();
            }
            if (ImGuiMCP::IsItemHovered()) {
                ImGuiMCP::SetTooltip("%s", "Third person only. The camera swings round behind where you're going; the "
                                           "right stick or mouse looks around and it eases back.");
            }
            ImGuiMCP::TextWrapped("%s", "Edge with the triggers or the mouse buttons (left/right edge): the skis carve "
                                        "an arc that tightens with more edge. Attacks, blocking and readying weapons "
                                        "are off while skiing.");
            ImGuiMCP::TextWrapped("%s", "Hold skid to twist the skis sideways and dig the edges in: a hockey stop. "
                                        "They twist toward the edge you're holding, or into the hill with no edge, "
                                        "further the longer you hold. Keep holding and you stay put on the hill. "
                                        "Release before you've stopped and they untwist straight away. Stop "
                                        "completely and the skis settle across the hill; release and you side-slip, "
                                        "with any edge you hold setting how much (tap skid to stop again), then they "
                                        "turn to point downhill. When nearly stopped, the left stick or A/D turn "
                                        "you on the spot. W and S do nothing yet. Telemetry is written to the log "
                                        "while enabled.");

            ImGuiMCP::SeparatorText("Compatibility");
            RenderTdm();
            RenderMovementChain();
            RenderCamera();
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

            ImGuiMCP::SeparatorText("Camera");
            auto camera = Settings::Camera();
            bool cameraChanged = false;
            for (const auto& field : Settings::CameraFields()) {
                cameraChanged |= ImGuiMCP::SliderFloat(field.label, &(camera.*field.member), field.min, field.max,
                    field.format);
                finished |= ImGuiMCP::IsItemDeactivatedAfterEdit();
                if (ImGuiMCP::IsItemHovered()) {
                    ImGuiMCP::SetTooltip("%s", field.help);
                }
            }
            if (cameraChanged) {
                Settings::SetCamera(camera);
            }

            ImGuiMCP::Spacing();
            if (ImGuiMCP::Button("Reset to defaults")) {
                Settings::SetPhysics({});
                Settings::SetCamera({ .enabled = Settings::Camera().enabled });
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
