#pragma once

#include "core/Vec3.h"

namespace SkiingInSkyrim::Core
{
    // Where one ski's mesh goes, in the character's frame: x right, y forward, z up, origin on the ground
    // under the character. The axes are where the mesh's own x, y (tips) and z (up from the running
    // surface) end up; the mesh's origin goes at position.
    struct SkiPose
    {
        Vec3 right{ 1.0F, 0.0F, 0.0F };
        Vec3 forward{ 0.0F, 1.0F, 0.0F };
        Vec3 up{ 0.0F, 0.0F, 1.0F };
        Vec3 position{};
    };

    // A ski lying on the ground plane (groundNormal, in the character's frame), pointing along yaw (radians
    // from the character's facing, positive to the right), rolled onto its edge (radians, positive puts the
    // right edge down) and lifted so that edge rather than its centre meets the snow. lateral is how far
    // right of the character it sits, in the same units as halfWidth.
    [[nodiscard]] SkiPose PlaceSki(Vec3 groundNormal, float yaw, float edgeRadians, float lateral, float halfWidth) noexcept;
}
