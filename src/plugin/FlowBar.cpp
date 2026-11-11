#include "plugin/FlowBar.h"

#include "plugin/Skiing.h"
#include "plugin/SkiingState.h"

#include <SKSEMenuFramework.h>

namespace SkiingInSkyrim::FlowBar
{
    namespace
    {
        using ImGuiMCP::ImU32;
        using ImGuiMCP::ImVec2;
        namespace Draw = ImGuiMCP::ImDrawListManager;

        // At 1080 lines; scaled with the screen's height.
        constexpr float kWidth = 200.0F;
        constexpr float kHeight = 7.0F;
        constexpr float kMargin = 44.0F;
        constexpr float kGlow = 4.0F;
        constexpr float kEaseSeconds = 0.15F;
        constexpr float kFillFlashSeconds = 0.9F;
        constexpr float kHitFlashSeconds = 0.5F;

        constexpr ImU32 Color(int r, int g, int b, float alpha)
        {
            return IM_COL32(r, g, b, static_cast<int>(std::clamp(alpha, 0.0F, 1.0F) * 255.0F));
        }

        // In a save, with nothing over the game: no paused menu (inventory, map, journal, console), no
        // conversation, and no menu framework window.
        bool Playing()
        {
            auto* ui = RE::UI::GetSingleton();
            if (!ui || ui->GameIsPaused() || ui->IsMenuOpen(RE::MainMenu::MENU_NAME) ||
                ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) || ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) ||
                SKSEMenuFramework::IsAnyBlockingWindowOpened()) {
                return false;
            }
            auto* player = RE::PlayerCharacter::GetSingleton();
            return player && player->Is3DLoaded();
        }

        float Ease(float from, float to, float delta, float seconds)
        {
            return from + (to - from) * (1.0F - std::exp(-std::max(0.0F, delta) / seconds));
        }
    }

    void __stdcall Render()
    {
        static float         shown = 0.0F;
        static std::uint32_t fills = 0;
        static std::uint32_t absorbs = 0;
        static float         fillFlash = 0.0F;
        static float         hitFlash = 0.0F;

        const auto flow = Skiing::GetFlow();
        auto*      io = ImGuiMCP::GetIO();
        const float delta = io ? std::clamp(io->DeltaTime, 0.0F, 0.1F) : 0.0F;
        if (std::exchange(fills, flow.fills) != flow.fills) {
            shown = 1.0F;
            fillFlash = kFillFlashSeconds;
        }
        if (std::exchange(absorbs, flow.absorbs) != flow.absorbs) {
            hitFlash = kHitFlashSeconds;
        }
        shown = Ease(shown, flow.shown, delta, kEaseSeconds);
        fillFlash = std::max(0.0F, fillFlash - delta);
        hitFlash = std::max(0.0F, hitFlash - delta);
        if (!flow.enabled || !AreSkisWorn() || !io || !Playing()) {
            return;
        }
        auto* list = ImGuiMCP::GetForegroundDrawList();
        if (!list) {
            return;
        }

        const float  scale = io->DisplaySize.y > 0.0F ? io->DisplaySize.y / 1080.0F : 1.0F;
        const float  width = kWidth * scale;
        const float  height = kHeight * scale;
        const ImVec2 from{ io->DisplaySize.x - kMargin * scale - width, kMargin * scale };
        const ImVec2 to{ from.x + width, from.y + height };
        const float  round = height * 0.5F;

        if (const float flash = std::max(fillFlash / kFillFlashSeconds, hitFlash / kHitFlashSeconds); flash > 0.0F) {
            const float glow = kGlow * scale * (1.0F + flash);
            Draw::AddRectFilled(list, { from.x - glow, from.y - glow }, { to.x + glow, to.y + glow },
                Color(255, 200, 70, 0.45F * flash), round + glow, 0);
        }
        Draw::AddRectFilled(list, from, to, Color(0, 0, 0, 0.45F), round, 0);
        if (shown > 0.005F) {
            const ImU32 fill = flow.full ? Color(255, 196, 60, 1.0F) : Color(255, 255, 255, 0.8F);
            Draw::AddRectFilled(list, from, { from.x + std::max(width * shown, height), to.y }, fill, round, 0);
        }
        Draw::AddRect(list, from, to, Color(255, 255, 255, 0.25F), round, 0, std::max(1.0F, scale));
    }
}
