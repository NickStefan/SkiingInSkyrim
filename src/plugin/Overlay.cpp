#include "plugin/Overlay.h"

#include "plugin/Settings.h"
#include "plugin/Skiing.h"

#include <SKSEMenuFramework.h>

namespace SkiingInSkyrim::Overlay
{
    namespace
    {
        using ImGuiMCP::ImU32;
        using ImGuiMCP::ImVec2;
        namespace Draw = ImGuiMCP::ImDrawListManager;

        constexpr float kRadians = std::numbers::pi_v<float> / 180.0F;
        constexpr float kLeft = 20.0F;
        constexpr float kTop = 20.0F;
        constexpr float kWidth = 300.0F;
        constexpr float kHeight = 190.0F;
        constexpr float kCompassRadius = 60.0F;
        // The travel marker reaches its full length at this speed.
        constexpr float kFullScaleSpeed = 15.0F;
        constexpr float kMphPerMetrePerSecond = 2.23694F;

        const ImU32 kBackground = IM_COL32(0, 0, 0, 140);
        const ImU32 kFaint = IM_COL32(255, 255, 255, 60);
        const ImU32 kSki = IM_COL32(255, 255, 255, 255);
        const ImU32 kText = IM_COL32(230, 230, 230, 255);
        const ImU32 kFallLine = IM_COL32(90, 160, 255, 255);
        const ImU32 kGrip = IM_COL32(110, 220, 110, 255);
        const ImU32 kSkid = IM_COL32(255, 160, 50, 255);
        const ImU32 kSkate = IM_COL32(90, 220, 255, 255);
        const ImU32 kFaintText = IM_COL32(230, 230, 230, 150);

        // A save is loaded and on screen: no main or loading menu, and the player's 3D exists.
        bool InGame()
        {
            auto* ui = RE::UI::GetSingleton();
            if (!ui || ui->IsMenuOpen(RE::MainMenu::MENU_NAME) || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
                return false;
            }
            auto* player = RE::PlayerCharacter::GetSingleton();
            return player && player->Is3DLoaded();
        }

        ImVec2 Add(ImVec2 a, ImVec2 b) { return { a.x + b.x, a.y + b.y }; }
        ImVec2 Scale(ImVec2 v, float s) { return { v.x * s, v.y * s }; }

        void Arrow(ImGuiMCP::ImDrawList* list, ImVec2 from, ImVec2 direction, float length, ImU32 color, float thickness)
        {
            const ImVec2 tip = Add(from, Scale(direction, length));
            const ImVec2 side{ -direction.y, direction.x };
            const float  head = std::min(10.0F, length * 0.4F);
            Draw::AddLine(list, from, Add(tip, Scale(direction, -head * 0.5F)), color, thickness);
            Draw::AddTriangleFilled(list, tip, Add(Add(tip, Scale(direction, -head)), Scale(side, head * 0.5F)),
                Add(Add(tip, Scale(direction, -head)), Scale(side, -head * 0.5F)), color);
        }

        void Text(ImGuiMCP::ImDrawList* list, ImVec2 at, ImU32 color, const std::string& text)
        {
            Draw::AddText(list, at, color, text.c_str());
        }

        // Screen direction of a world direction, with the reference direction (the fall line) up the screen.
        ImVec2 Relative(Core::Vec2 direction, float reference)
        {
            const float angle = std::atan2(direction.x, direction.y) - reference;
            return { std::sin(angle), -std::cos(angle) };
        }

        // Top-down with the fall line fixed up the screen: the skis through the centre, and the direction of
        // travel as a marker outside the rim. On flat ground the last fall line seen is kept.
        void Compass(ImGuiMCP::ImDrawList* list, ImVec2 center, const Skiing::OverlayData& data)
        {
            static float fallLine = 0.0F;
            if (data.grounded && data.slopeDegrees > 1.0F && Core::Length(data.downhill) > 1e-4F) {
                fallLine = std::atan2(data.downhill.x, data.downhill.y);
            }

            Draw::AddCircle(list, center, kCompassRadius, kFaint, 48, 1.5F);
            const float  steepness = std::min(1.0F, std::sin(data.slopeDegrees * kRadians) / std::sin(45.0F * kRadians));
            const ImVec2 rim{ center.x, center.y - kCompassRadius };
            Arrow(list, Add(rim, { 0.0F, 14.0F }), { 0.0F, -1.0F }, 14.0F + 10.0F * steepness, kFallLine, 3.0F);

            const ImVec2 ski = Relative(Core::Forward(data.yaw), fallLine);
            Draw::AddLine(list, Add(center, Scale(ski, -kCompassRadius * 0.8F)), Add(center, Scale(ski, kCompassRadius * 0.8F)),
                kSki, 5.0F);
            const ImVec2 side{ -ski.y, ski.x };
            const ImVec2 tip = Add(center, Scale(ski, kCompassRadius * 0.95F));
            const ImVec2 base = Add(center, Scale(ski, kCompassRadius * 0.8F));
            Draw::AddTriangleFilled(list, tip, Add(base, Scale(side, 5.0F)), Add(base, Scale(side, -5.0F)), kSki);

            const float speed = Core::Length(data.velocity);
            if (speed > 0.2F) {
                const ImVec2 travel = Relative(data.velocity, fallLine);
                const float  length = 10.0F + 14.0F * std::min(1.0F, speed / kFullScaleSpeed);
                Arrow(list, Add(center, Scale(travel, kCompassRadius + 4.0F)), travel, length,
                    data.skidding ? kSkid : kGrip, 3.0F);
            }
        }

        // Seen from behind: the ski tilted by the edge angle, with the maximum edge marked.
        void EdgeGauge(ImGuiMCP::ImDrawList* list, ImVec2 center, const Skiing::OverlayData& data)
        {
            constexpr float kHalf = 45.0F;
            Draw::AddLine(list, Add(center, { -kHalf, 12.0F }), Add(center, { kHalf, 12.0F }), kFaint, 1.0F);
            for (const float side : { -1.0F, 1.0F }) {
                const float  max = side * data.maxEdgeDegrees * kRadians;
                const ImVec2 end{ std::cos(max) * kHalf * 0.6F, std::sin(max) * kHalf * 0.6F };
                Draw::AddLine(list, Add(center, Scale(end, -1.0F)), Add(center, end), kFaint, 1.0F);
            }
            const float  edge = data.edgeDegrees * kRadians;
            const ImVec2 end{ std::cos(edge) * kHalf, std::sin(edge) * kHalf };
            Draw::AddLine(list, Add(center, Scale(end, -1.0F)), Add(center, end), data.skidding ? kSkid : kGrip, 5.0F);
        }
    }

    void __stdcall Render()
    {
        if (!Settings::ShowOverlay() || !InGame()) {
            return;
        }
        auto* list = ImGuiMCP::GetForegroundDrawList();
        if (!list) {
            return;
        }
        const auto data = Skiing::GetOverlay();
        const ImVec2 origin{ kLeft, kTop };
        if (!data.active) {
            return;
        }

        Draw::AddRectFilled(list, origin, Add(origin, { kWidth, kHeight }), kBackground, 6.0F, 0);
        Text(list, Add(origin, { 10.0F, 6.0F }), data.skating ? kSkate : kFaintText, data.skating ? "skating" : "skiing");
        Compass(list, Add(origin, { 95.0F, 95.0F }), data);
        EdgeGauge(list, Add(origin, { 240.0F, 55.0F }), data);
        Text(list, Add(origin, { 205.0F, 112.0F }), kText,
            fmt::format("{:.0f} mph", Core::Length(data.velocity) * kMphPerMetrePerSecond));
        Text(list, Add(origin, { 205.0F, 136.0F }), data.grounded ? kText : kFaintText,
            data.grounded ? fmt::format("{:.0f}\u00B0", data.slopeDegrees) : std::string("airborne"));
        Text(list, Add(origin, { 10.0F, 166.0F }), kFaintText,
            fmt::format("{} | powder {:.0f}%", data.material, data.powder * 100.0F));
    }
}
