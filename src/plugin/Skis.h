#pragma once

#include "core/Vec3.h"

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

    struct Pose
    {
        float      yaw{ 0.0F };   // ski heading, radians, world
        float      edgeRadians{ 0.0F };  // signed, positive = right edge
        Core::Vec3 groundNormal{ 0.0F, 0.0F, 1.0F };  // world
    };

    // Main thread, every frame while skiing: draws the worn skis' model under the feet.
    void Show(RE::PlayerCharacter* player, RE::TESObjectARMO* skis, const Pose& pose, float delta);
    // Main thread: takes them away again.
    void Hide();
}
