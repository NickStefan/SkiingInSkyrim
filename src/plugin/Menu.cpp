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
            RenderBinding(Binding::SkidKeyboard, "Skid (keyboard/mouse)", "press a key or mouse button (Esc cancels)");
            RenderBinding(Binding::SkidGamepad, "Skid (gamepad)", "press a gamepad button (Esc cancels)");
        }

        void Tooltip(const char* help)
        {
            if (ImGuiMCP::IsItemHovered()) {
                ImGuiMCP::SetTooltip("%s", help);
            }
        }

        // A checkbox for an on/off setting kept outside the field tables, saved as soon as it's clicked.
        void Toggle(const char* label, bool (*get)(), void (*set)(bool), const char* help)
        {
            bool value = get();
            if (ImGuiMCP::Checkbox(label, &value)) {
                set(value);
                Settings::Save();
            }
            Tooltip(help);
        }

        // Sliders save when released, so dragging one doesn't write the file every frame.
        template <class Owner, class Field>
        void Sliders(Owner& values, std::span<const Field> fields, bool& changed, bool& finished)
        {
            for (const auto& field : fields) {
                changed |= ImGuiMCP::SliderFloat(field.label, &(values.*field.member), field.min, field.max, field.format);
                finished |= ImGuiMCP::IsItemDeactivatedAfterEdit();
                Tooltip(field.help);
            }
        }

        template <class Owner, class Field>
        void Checkboxes(Owner& values, std::span<const Field> fields, bool& changed, bool& finished)
        {
            for (const auto& field : fields) {
                if (ImGuiMCP::Checkbox(field.label, &(values.*field.member))) {
                    changed = finished = true;
                }
                Tooltip(field.help);
            }
        }

        void Hint()
        {
            ImGuiMCP::TextDisabled("%s", "Saved to Data/SKSE/Plugins/SkiingInSkyrim.ini when you release a slider. "
                                         "Ctrl+click a slider to type a value.");
        }

        bool ResetButton() { return ImGuiMCP::Button("Reset this page to defaults"); }

        // The physics settings on one page, under their group headings, with a reset for just those.
        void PhysicsPage(Settings::Page page)
        {
            auto physics = Settings::Physics();
            bool changed = false;
            bool finished = false;
            for (const auto& group : Settings::PhysicsGroups()) {
                if (group.page != page) {
                    continue;
                }
                if (*group.title) {
                    ImGuiMCP::SeparatorText(group.title);
                }
                if (group.intro) {
                    ImGuiMCP::TextWrapped("%s", group.intro);
                }
                Checkboxes(physics, group.switches, changed, finished);
                Sliders(physics, group.fields, changed, finished);
            }
            ImGuiMCP::Spacing();
            if (ResetButton()) {
                const Core::SkiSettings defaults;
                for (const auto& group : Settings::PhysicsGroups()) {
                    if (group.page != page) {
                        continue;
                    }
                    for (const auto& field : group.switches) {
                        physics.*field.member = defaults.*field.member;
                    }
                    for (const auto& field : group.fields) {
                        physics.*field.member = defaults.*field.member;
                    }
                }
                changed = finished = true;
            }
            if (changed) {
                Settings::SetPhysics(physics);
            }
            if (finished) {
                Settings::Save();
            }
        }

        void __stdcall RenderGeneral()
        {
            bool enabled = Settings::ModEnabled();
            if (ImGuiMCP::Checkbox("Mod enabled", &enabled)) {
                Settings::SetModEnabled(enabled);
                Settings::Save();
            }
            Tooltip("Off, wearing skis does nothing: handy for telling whether a problem comes from this mod.");
            ImGuiMCP::SameLine();
            if (!enabled) {
                ImGuiMCP::TextDisabled("%s", "(skis do nothing)");
            } else if (AreSkisWorn()) {
                Colored(kGood, "Skis on: skiing");
            } else {
                ImGuiMCP::TextDisabled("%s", "Put on skis to ski (craft them at a forge)");
            }
            Toggle("Show debug overlay", Settings::ShowOverlay, Settings::SetShowOverlay,
                "Speed, slope, edge and the skis' direction on screen while skiing.");
            Toggle("Log jitter trace", Settings::TraceShaking, Settings::SetTraceShaking,
                "Writes every frame to the log while the character shakes up and down. Only for chasing jitter; "
                "leave off otherwise.");
            ImGuiMCP::TextWrapped("%s", "Wear skis (Wooden Skis: firewood and leather strips at a forge) to ski; "
                                        "take them off to walk. Attacks, blocking and readying weapons are off while "
                                        "skiing. Landing along a slope is soft; landing flat or skiing into trees, "
                                        "rocks and walls hurts (see Impacts). Telemetry is written to the log while "
                                        "enabled.");

            ImGuiMCP::SeparatorText("Compatibility");
            RenderTdm();
            RenderMovementChain();
            RenderCamera();
        }

        void __stdcall RenderControls()
        {
            RenderBindings();
            Toggle("Looking around steers (keyboard/mouse)", Settings::LookSteersKeyboardMouse,
                Settings::SetLookSteersKeyboardMouse,
                "W/A/S/D steer relative to where you look, so moving the mouse while holding a key turns the skis. "
                "Off, they steer relative to the chase camera and the mouse only looks around.");
            Toggle("Looking around steers (gamepad)", Settings::LookSteersGamepad, Settings::SetLookSteersGamepad,
                "The left stick steers relative to where you look, so the right stick turns the skis while the left "
                "is pushed. Off, it steers relative to the chase camera and the right stick only looks around.");
            ImGuiMCP::TextWrapped("%s", "Edge with the triggers or the mouse buttons (left/right edge): the skis carve "
                                        "an arc that tightens with more edge.");
            ImGuiMCP::TextWrapped("%s", "Steer with the left stick or W/A/S/D: the skis "
                                        "turn toward where you push, relative to the camera, more firmly the further "
                                        "you push. Push firmly the way the skis already point to tuck and go faster. "
                                        "On gentle ground, push forward and squeeze the triggers alternately to skate. "
                                        "Holding a clean carve through a turn keeps your speed up. "
                                        "Leave it centred and the skis carve wherever the edges take them; with no "
                                        "edge either, skis smearing sideways at speed swing back round to where "
                                        "you're going. Steering harder than they can carve skids them round and "
                                        "scrubs speed.");
            ImGuiMCP::TextWrapped("%s", "Hold skid to twist the skis sideways and dig the edges in: a hockey stop. "
                                        "They twist toward the edge you're holding, or into the hill with no edge, "
                                        "further the longer you hold. Keep holding and you stay put on the hill. "
                                        "Release before you've stopped and they untwist; stop completely and they "
                                        "stay where they are. Sliding slowly sideways, the uphill edge feathers the "
                                        "slip: none slides, full holds.");
            PhysicsPage(Settings::Page::Controls);
        }

        void __stdcall RenderSpeed()
        {
            Hint();
            PhysicsPage(Settings::Page::Speed);
        }

        void __stdcall RenderBody()
        {
            Toggle("Ski stance", Settings::StanceEnabled, Settings::SetStanceEnabled,
                "Pose the body like a skier while skiing: knees bent, leaning forward, arms out front, crouching "
                "into a tuck. Off, the game's own animation shows.");
            Hint();
            auto stance = Settings::Stance();
            bool changed = false;
            bool finished = false;
            Checkboxes(stance, Settings::StanceSwitches(), changed, finished);
            Sliders(stance, Settings::StanceFields(), changed, finished);
            ImGuiMCP::Spacing();
            if (ResetButton()) {
                stance = {};
                changed = finished = true;
            }
            if (changed) {
                Settings::SetStance(stance);
            }
            if (finished) {
                Settings::Save();
            }
        }

        void __stdcall RenderCameraPage()
        {
            auto camera = Settings::Camera();
            bool changed = false;
            bool finished = false;
            if (ImGuiMCP::Checkbox("Camera follows direction of travel", &camera.enabled)) {
                changed = finished = true;
            }
            Tooltip("Third person only. The camera swings round behind where you're going; the right stick or "
                    "mouse looks around and it eases back.");
            Hint();
            Sliders(camera, Settings::CameraFields(), changed, finished);
            ImGuiMCP::Spacing();
            if (ResetButton()) {
                camera = { .enabled = camera.enabled };
                changed = finished = true;
            }
            if (changed) {
                Settings::SetCamera(camera);
            }
            if (finished) {
                Settings::Save();
            }
        }

        void __stdcall RenderStamina()
        {
            Hint();
            PhysicsPage(Settings::Page::Stamina);
        }

        void __stdcall RenderImpacts()
        {
            ImGuiMCP::TextWrapped("%s", "On skis, the game's own fall damage is off. Instead, landings hurt by how "
                                        "fast you meet the slope (landing along a steep slope going downhill is soft; "
                                        "dropping flat is not), and skiing into trees, rocks and walls hurts by the "
                                        "speed lost. Both use the game's fall-damage curve, scaled below.");
            Hint();
            PhysicsPage(Settings::Page::Impacts);
        }

        bool __stdcall OnFrameworkInput(RE::InputEvent* event) { return Input::OnMenuFrameworkInput(event); }
    }

    bool Register()
    {
        if (!SKSEMenuFramework::IsInstalled()) {
            return false;
        }
        SKSEMenuFramework::SetSection("Skiing in Skyrim");
        SKSEMenuFramework::AddSectionItem("General", RenderGeneral);
        SKSEMenuFramework::AddSectionItem("Controls", RenderControls);
        SKSEMenuFramework::AddSectionItem("Speed", RenderSpeed);
        SKSEMenuFramework::AddSectionItem("Body", RenderBody);
        SKSEMenuFramework::AddSectionItem("Camera", RenderCameraPage);
        SKSEMenuFramework::AddSectionItem("Stamina", RenderStamina);
        SKSEMenuFramework::AddSectionItem("Impacts", RenderImpacts);
        SKSEMenuFramework::AddInputEvent(OnFrameworkInput);
        SKSEMenuFramework::AddHudElement(Overlay::Render);
        return true;
    }
}
