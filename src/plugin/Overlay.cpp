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
        constexpr float kHeight = 250.0F;
        constexpr float kCompassRadius = 60.0F;
        // Velocity arrow reaches the compass rim at this speed.
        constexpr float kFullScaleSpeed = 15.0F;

        const ImU32 kBackground = IM_COL32(0, 0, 0, 140);
        const ImU32 kFaint = IM_COL32(255, 255, 255, 60);
        const ImU32 kSki = IM_COL32(255, 255, 255, 255);
        const ImU32 kText = IM_COL32(230, 230, 230, 255);
        const ImU32 kFallLine = IM_COL32(90, 160, 255, 255);
        const ImU32 kGrip = IM_COL32(110, 220, 110, 255);
        const ImU32 kSkid = IM_COL32(255, 160, 50, 255);

        ImVec2 Add(ImVec2 a, ImVec2 b) { return { a.x + b.x, a.y + b.y }; }
        ImVec2 Scale(ImVec2 v, float s) { return { v.x * s, v.y * s }; }

        // Screen direction of a world direction, with the skis pointing up the screen.
        ImVec2 RelativeToSkis(Core::Vec2 direction, float yaw)
        {
            const float angle = std::atan2(direction.x, direction.y) - yaw;
            return { std::sin(angle), -std::cos(angle) };
        }

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

        // Top-down: skis up the screen, the fall line and the direction of travel relative to them.
        void Compass(ImGuiMCP::ImDrawList* list, ImVec2 center, const Skiing::OverlayData& data)
        {
            Draw::AddCircle(list, center, kCompassRadius, kFaint, 48, 1.5F);
            Draw::AddLine(list, Add(center, { -kCompassRadius, 0.0F }), Add(center, { kCompassRadius, 0.0F }), kFaint, 1.0F);

            const float downhill = Core::Length(data.downhill);
            if (data.grounded && data.slopeDegrees > 1.0F && downhill > 1e-4F) {
                const float steepness = std::min(1.0F, std::sin(data.slopeDegrees * kRadians) / std::sin(45.0F * kRadians));
                Arrow(list, center, RelativeToSkis(data.downhill, data.yaw), kCompassRadius * (0.25F + 0.75F * steepness),
                    kFallLine, 2.0F);
            }

            Draw::AddLine(list, Add(center, { 0.0F, kCompassRadius * 0.8F }), Add(center, { 0.0F, -kCompassRadius * 0.8F }),
                kSki, 5.0F);
            Draw::AddTriangleFilled(list, Add(center, { 0.0F, -kCompassRadius * 0.95F }),
                Add(center, { -5.0F, -kCompassRadius * 0.8F }), Add(center, { 5.0F, -kCompassRadius * 0.8F }), kSki);

            const float speed = Core::Length(data.velocity);
            if (speed > 0.2F) {
                const float length = kCompassRadius * std::min(1.0F, 0.2F + 0.8F * speed / kFullScaleSpeed);
                Arrow(list, center, RelativeToSkis(data.velocity, data.yaw), length, data.skidding ? kSkid : kGrip, 3.0F);
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

        // Applied pivot as a bar: centre is none, ends are the full pivot left/right.
        void PivotBar(ImGuiMCP::ImDrawList* list, ImVec2 center, const Skiing::OverlayData& data)
        {
            constexpr float kHalf = 45.0F;
            Draw::AddRect(list, Add(center, { -kHalf, -4.0F }), Add(center, { kHalf, 4.0F }), kFaint, 2.0F, 0, 1.0F);
            const float fraction = data.maxPivotDegrees > 0.0F ? data.pivotDegrees / data.maxPivotDegrees : 0.0F;
            const float x = std::clamp(fraction, -1.0F, 1.0F) * kHalf;
            Draw::AddRectFilled(list, Add(center, { std::min(0.0F, x), -4.0F }), Add(center, { std::max(0.0F, x), 4.0F }),
                kSki, 2.0F, 0);
        }
    }

    void __stdcall Render()
    {
        if (!Settings::ShowOverlay()) {
            return;
        }
        auto* list = ImGuiMCP::GetForegroundDrawList();
        if (!list) {
            return;
        }
        const auto data = Skiing::GetOverlay();
        const ImVec2 origin{ kLeft, kTop };
        if (!data.active) {
            Draw::AddRectFilled(list, origin, Add(origin, { kWidth, 28.0F }), kBackground, 6.0F, 0);
            Text(list, Add(origin, { 10.0F, 6.0F }), kText, fmt::format("Skiing in Skyrim: {}", data.inactiveReason));
            return;
        }

        Draw::AddRectFilled(list, origin, Add(origin, { kWidth, kHeight }), kBackground, 6.0F, 0);
        Compass(list, Add(origin, { 80.0F, 80.0F }), data);
        EdgeGauge(list, Add(origin, { 230.0F, 50.0F }), data);
        Text(list, Add(origin, { 185.0F, 80.0F }), kText, fmt::format("edge {:+.0f} deg", data.edgeDegrees));
        PivotBar(list, Add(origin, { 230.0F, 120.0F }), data);
        Text(list, Add(origin, { 185.0F, 130.0F }), kText, fmt::format("pivot {:+.0f} deg", data.pivotDegrees));

        const float speed = Core::Length(data.velocity);
        float       y = kTop + 160.0F;
        const auto  line = [&](ImU32 color, const std::string& text) {
            Text(list, { kLeft + 10.0F, y }, color, text);
            y += 17.0F;
        };
        line(kText, fmt::format("speed {:.1f} m/s   slope {:.0f} deg", speed, data.slopeDegrees));
        line(kText, speed > 0.2F ? fmt::format("slip {:+.0f} deg   grip {:.1f} m/s2", data.slipDegrees, data.gripLimit) :
                                   fmt::format("slip -   grip {:.1f} m/s2", data.gripLimit));
        const char* hold = !data.grounded ? "airborne" : data.skidding ? "skidding" : "holding";
        line(data.skidding ? kSkid : kGrip, data.parked ? fmt::format("{}   side-slip mode", hold) : hold);
        line(kFaint, "white: skis  blue: fall line  arrow: travel");
    }
}
