#pragma once

namespace SkiingInSkyrim::Core
{
    // Flow: skiing down without falling fills a bar, by height descended. Full, it's a reserve, in metres of
    // fall, that a hard landing or crash is taken from instead of hurting or knocking you down. A hit bigger than
    // what's left is still let off, and empties it. Empty, it fills again from nothing; a tumble while it fills
    // starts it over.
    struct FlowSettings
    {
        float fillMetres{ 150.0F };    // height descended on skis to fill the bar
        float reserveMetres{ 25.0F };  // metres of fall a full bar takes
    };

    struct FlowState
    {
        float descended{ 0.0F };  // metres, while filling
        float reserve{ 0.0F };    // metres of fall left; above 0, the bar is full and protecting

        [[nodiscard]] bool Full() const noexcept { return reserve > 0.0F; }
    };

    // Metres descended this frame (drops only; climbing doesn't take any back). True the frame it fills.
    bool FlowDescend(FlowState& state, float metres, const FlowSettings& settings) noexcept;

    // A tumble: while filling, the bar starts over.
    void FlowFall(FlowState& state) noexcept;

    // A hit as hard as falling this many metres, which would hurt or knock you down: taken from a full bar
    // (true: let off), or left to do its harm (false).
    bool FlowAbsorb(FlowState& state, float metres) noexcept;

    // 0..1 for the bar: how far it has filled, or, full, how much of the reserve is left.
    [[nodiscard]] float FlowShown(const FlowState& state, const FlowSettings& settings) noexcept;
}
