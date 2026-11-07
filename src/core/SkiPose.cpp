#include "core/SkiPose.h"

namespace SkiingInSkyrim::Core
{
    namespace
    {
        // Ground steeper than this (normal.z below it) isn't something to lie skis on; treat it as level.
        constexpr float kMinNormalZ = 0.2F;

        Vec3 Scale(Vec3 v, float s) { return { v.x * s, v.y * s, v.z * s }; }
        Vec3 Add(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
        Vec3 Cross(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }

        Vec3 Normalized(Vec3 v, Vec3 fallback)
        {
            const float length = Length(v);
            return length > 1e-6F ? Scale(v, 1.0F / length) : fallback;
        }
    }

    SkiPose PlaceSki(Vec3 groundNormal, float yaw, float edgeRadians, float lateral, float halfWidth) noexcept
    {
        Vec3 normal = Normalized(groundNormal, { 0.0F, 0.0F, 1.0F });
        if (normal.z < kMinNormalZ) {
            normal = { 0.0F, 0.0F, 1.0F };
        }
        const Vec3 heading{ std::sin(yaw), std::cos(yaw), 0.0F };
        const Vec3 forward = Normalized(Add(heading, Scale(normal, -Dot(heading, normal))), { 0.0F, 1.0F, 0.0F });
        const Vec3 flatRight = Cross(forward, normal);

        SkiPose pose;
        const float cosine = std::cos(edgeRadians);
        const float sine = std::sin(edgeRadians);
        pose.forward = forward;
        pose.up = Add(Scale(normal, cosine), Scale(flatRight, sine));
        pose.right = Add(Scale(flatRight, cosine), Scale(normal, -sine));

        const float groundZ = -normal.x * lateral / normal.z;
        pose.position = Add({ lateral, 0.0F, groundZ }, Scale(normal, halfWidth * std::fabs(sine)));
        return pose;
    }
}
