#pragma once

#include "core/SkiPose.h"
#include "plugin/Skiing.h"

namespace SkiingInSkyrim::Skis
{
    // Armour with this keyword is a pair of skis: wearing it turns skiing on.
    inline constexpr std::string_view kKeyword = "SkiingInSkyrim_Skis";

    // Once game data is loaded: watches the player's equipment.
    void Register();

    // Game load: forget the old skis before the old 3D goes.
    void Reset();
    // New game or loaded save: look at what's worn again.
    void Refresh();

    // Main thread. The skis the player wears, if any.
    [[nodiscard]] RE::TESObjectARMO* Worn(RE::PlayerCharacter* player);

    // Main thread: loads the skis' model, when they change.
    void Prepare(RE::TESObjectARMO* skis);

    // Half a ski's width, game units: how far an edged ski is lifted onto its edge.
    inline constexpr float kHalfWidth = 5.0F;

    // Animation thread, after the body is posed: draws the skis under the feet while skiing, and takes them
    // away otherwise. The skis go where posed says (left, right; relative to the character) if given,
    // otherwise under the feet wherever the animation put them.
    void Place(RE::PlayerCharacter* player, const Skiing::Body& body, float delta,
        const std::array<Core::SkiPose, 2>* posed);
}
