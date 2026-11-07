#pragma once

namespace SkiingInSkyrim
{
    // The mod is enabled and the player wears skis. Any thread.
    [[nodiscard]] bool IsSkiingEnabled();
    [[nodiscard]] bool AreSkisWorn() noexcept;
    void SetSkisWorn(bool worn) noexcept;
}
