#pragma once

namespace SkiingInSkyrim::Pose
{
    // At plugin load: poses the body (and places the skis) right after the player's animation update,
    // which runs off the main thread and is the last thing to set the skeleton before it's drawn.
    void Install();

    // Main thread: reads the standing pose from the player's skeleton, when it changes.
    void Prepare(RE::PlayerCharacter* player);
}
