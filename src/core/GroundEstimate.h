#pragma once

#include "core/Vec3.h"

namespace SkiingInSkyrim::Core
{
    // One frame of raw character-controller data.
    struct GroundSample
    {
        bool  supported{ false };
        Vec3  surfaceNormal{};
        Vec3  supportNormal{};
        Vec3  position{};  // game units
        Vec3  velocity{};  // m/s
        float deltaSeconds{ 0.0F };
    };

    struct GroundSettings
    {
        // Contacts steeper than this are walls (rocks, cliffs), not ski slopes.
        float wallSlopeDegrees{ 60.0F };
        // Losing support for less than this is contact flicker, not air.
        float airGraceSeconds{ 0.3F };
        // Falling this much faster than the slope explains means airborne immediately.
        float fallMarginMetersPerSecond{ 3.0F };
        // Distance over which the ground normal is averaged.
        float smoothingDistanceUnits{ 70.0F };
        // Keeps the normal converging slowly while standing still.
        float stationarySmoothingUnitsPerSecond{ 20.0F };
        // A jump bigger than this in one frame is a teleport or load; start over.
        float teleportDistanceUnits{ 500.0F };
    };

    enum class Contact
    {
        Grounded,
        Airborne
    };

    enum class NormalSource
    {
        None,
        Support,
        Surface
    };

    struct GroundEstimate
    {
        Contact      contact{ Contact::Grounded };
        bool         hasNormal{ false };
        Vec3         normal{ 0.0F, 0.0F, 1.0F };  // smoothed, unit length
        NormalSource source{ NormalSource::None };
        bool         wallContact{ false };
        float        unsupportedSeconds{ 0.0F };
    };

    class GroundTracker
    {
    public:
        explicit GroundTracker(GroundSettings settings = {}) noexcept :
            settings_(settings)
        {}

        void Reset() noexcept;
        const GroundEstimate& Update(const GroundSample& sample) noexcept;
        [[nodiscard]] const GroundEstimate& Current() const noexcept { return estimate_; }

    private:
        GroundSettings settings_;
        GroundEstimate estimate_;
        bool           hasPosition_{ false };
        Vec3           lastPosition_{};
    };
}
