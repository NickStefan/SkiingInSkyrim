#pragma once

#include "core/Vec3.h"

#include <optional>

namespace SkiingInSkyrim::Core
{
    // Speed the world takes away all at once: what the physics left of last frame's velocity (plus gravity, while
    // in the air) against what it was. Landing along a slope keeps nearly all of it, slamming flat or into a tree
    // loses the part going into the surface. Gaining speed (jumps, pushes) never counts.
    struct ImpactSettings
    {
        float gravity{ 9.81F };
        float startSpeed{ 3.0F };     // m/s lost in one frame to start an impact
        float continueSpeed{ 1.0F };  // m/s lost per frame to keep it going
        int   maxFrames{ 10 };
    };

    struct ImpactSample
    {
        Vec3  velocity;  // m/s, as the physics left it this frame
        bool  airborne;  // this frame
        float deltaSeconds;
    };

    struct Impact
    {
        Vec3  lost;    // m/s, summed over the impact: points into what was hit
        Vec3  before;  // m/s, the velocity expected the frame it started
        int   frames;
    };

    class ImpactTracker
    {
    public:
        explicit ImpactTracker(ImpactSettings settings = {}) noexcept : settings_(settings) {}

        // An impact, the frame it ends.
        std::optional<Impact> Update(const ImpactSample& sample) noexcept;

        // After this frame's velocity is changed by us (not by the world): the next frame expects this.
        void Override(const Vec3& velocity) noexcept { last_ = velocity; }

        // Teleports, load screens, skiing starting: forget the last velocity.
        void Reset() noexcept;

        [[nodiscard]] bool InImpact() const noexcept { return current_.has_value(); }

    private:
        std::optional<Impact> Finish() noexcept;

        ImpactSettings        settings_;
        std::optional<Vec3>   last_;
        bool                  lastAirborne_{ false };
        std::optional<Impact> current_;
    };

    // Metres of free fall that would land as hard as losing this much speed.
    [[nodiscard]] float EquivalentFallMetres(float speedLost, float gravity) noexcept;
}
