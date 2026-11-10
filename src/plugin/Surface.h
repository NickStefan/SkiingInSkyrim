#pragma once

#include <string>

namespace SkiingInSkyrim::Surface
{
    // What the skier is on, from the game's landscape and collision data.
    struct Reading
    {
        RE::MATERIAL_ID underfoot{ RE::MATERIAL_ID::kNone };  // what the character controller stands on
        RE::MATERIAL_ID land{ RE::MATERIAL_ID::kNone };       // the landscape's material at the feet
        std::string     texture;            // the landscape texture's diffuse file at the feet, lowercase
        bool            onLand{ false };    // standing on the landscape itself, not an object on it
        RE::COL_LAYER   below{ RE::COL_LAYER::kUnidentified };  // what's under the feet, by collision layer
        float           aboveLandUnits{ 0.0F };  // how far the feet are above the landscape's height
        float           powderCover{ 0.0F };     // 0..1 around the feet, snow off roads, paths and trails
    };

    // Main thread.
    Reading Read(RE::PlayerCharacter* player, RE::bhkCharacterController* controller);

    const char* MaterialName(RE::MATERIAL_ID material);
}
