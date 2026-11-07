#pragma once

#include "core/Vec3.h"

namespace SkiingInSkyrim::Skeleton
{
    // Main thread. A model from the game's model database (shared: copy it before changing or attaching it).
    // Paths as records store them, without "Meshes\".
    [[nodiscard]] RE::NiPointer<RE::NiNode> LoadModel(const std::string& path);

    // Where node is relative to root (one of its ancestors), from the local transforms in between; current
    // even when the world transforms haven't been updated yet this frame.
    [[nodiscard]] RE::NiTransform Relative(const RE::NiAVObject* node, const RE::NiAVObject* root);

    [[nodiscard]] RE::NiMatrix3 AxisAngle(const RE::NiPoint3& axis, float radians);
    // The smallest rotation taking direction from to direction to.
    [[nodiscard]] RE::NiMatrix3 Between(const RE::NiPoint3& from, const RE::NiPoint3& to);
    // From a (t = 0) to b (t = 1), along the shortest arc.
    [[nodiscard]] RE::NiMatrix3 Slerp(const RE::NiMatrix3& a, const RE::NiMatrix3& b, float t);

    // A world direction in the frame of a character facing heading (x right, y forward, z up): how the
    // character's 3D is turned.
    [[nodiscard]] inline Core::Vec3 ToCharacter(const Core::Vec3& world, float heading)
    {
        const float c = std::cos(heading);
        const float s = std::sin(heading);
        return { world.x * c - world.y * s, world.x * s + world.y * c, world.z };
    }

    [[nodiscard]] inline RE::NiMatrix3 FromAxes(const Core::Vec3& x, const Core::Vec3& y, const Core::Vec3& z)
    {
        RE::NiMatrix3 m;
        const Core::Vec3 axes[3] = { x, y, z };
        for (int column = 0; column < 3; ++column) {
            m.entry[0][column] = axes[column].x;
            m.entry[1][column] = axes[column].y;
            m.entry[2][column] = axes[column].z;
        }
        return m;
    }
}
