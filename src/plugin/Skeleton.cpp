#include "plugin/Skeleton.h"

namespace SkiingInSkyrim::Skeleton
{
    namespace
    {
        struct Quaternion
        {
            float w, x, y, z;
        };

        Quaternion ToQuaternion(const RE::NiMatrix3& m)
        {
            const auto& e = m.entry;
            const float trace = e[0][0] + e[1][1] + e[2][2];
            Quaternion q;
            if (trace > 0.0F) {
                const float s = std::sqrt(trace + 1.0F) * 2.0F;
                q = { 0.25F * s, (e[2][1] - e[1][2]) / s, (e[0][2] - e[2][0]) / s, (e[1][0] - e[0][1]) / s };
            } else if (e[0][0] > e[1][1] && e[0][0] > e[2][2]) {
                const float s = std::sqrt(1.0F + e[0][0] - e[1][1] - e[2][2]) * 2.0F;
                q = { (e[2][1] - e[1][2]) / s, 0.25F * s, (e[0][1] + e[1][0]) / s, (e[0][2] + e[2][0]) / s };
            } else if (e[1][1] > e[2][2]) {
                const float s = std::sqrt(1.0F + e[1][1] - e[0][0] - e[2][2]) * 2.0F;
                q = { (e[0][2] - e[2][0]) / s, (e[0][1] + e[1][0]) / s, 0.25F * s, (e[1][2] + e[2][1]) / s };
            } else {
                const float s = std::sqrt(1.0F + e[2][2] - e[0][0] - e[1][1]) * 2.0F;
                q = { (e[1][0] - e[0][1]) / s, (e[0][2] + e[2][0]) / s, (e[1][2] + e[2][1]) / s, 0.25F * s };
            }
            return q;
        }

        RE::NiMatrix3 ToMatrix(const Quaternion& q)
        {
            RE::NiMatrix3 m;
            auto& e = m.entry;
            e[0][0] = 1.0F - 2.0F * (q.y * q.y + q.z * q.z);
            e[0][1] = 2.0F * (q.x * q.y - q.z * q.w);
            e[0][2] = 2.0F * (q.x * q.z + q.y * q.w);
            e[1][0] = 2.0F * (q.x * q.y + q.z * q.w);
            e[1][1] = 1.0F - 2.0F * (q.x * q.x + q.z * q.z);
            e[1][2] = 2.0F * (q.y * q.z - q.x * q.w);
            e[2][0] = 2.0F * (q.x * q.z - q.y * q.w);
            e[2][1] = 2.0F * (q.y * q.z + q.x * q.w);
            e[2][2] = 1.0F - 2.0F * (q.x * q.x + q.y * q.y);
            return m;
        }
    }

    RE::NiPointer<RE::NiNode> LoadModel(const std::string& path)
    {
        const RE::BSModelDB::DBTraits::ArgsType args;
        for (const auto& candidate : { path, "Meshes\\" + path }) {
            RE::NiPointer<RE::NiNode> model;
            if (RE::BSModelDB::Demand(candidate.c_str(), model, args) == RE::BSResource::ErrorCode::kNone && model) {
                return model;
            }
        }
        return nullptr;
    }

    RE::NiTransform Relative(const RE::NiAVObject* node, const RE::NiAVObject* root)
    {
        RE::NiTransform result;
        for (const auto* at = node; at && at != root; at = at->parent) {
            result = at->local * result;
        }
        return result;
    }

    RE::NiMatrix3 AxisAngle(const RE::NiPoint3& axis, float radians)
    {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        const float t = 1.0F - c;
        RE::NiMatrix3 m;
        m.entry[0][0] = c + t * axis.x * axis.x;
        m.entry[0][1] = t * axis.x * axis.y - s * axis.z;
        m.entry[0][2] = t * axis.x * axis.z + s * axis.y;
        m.entry[1][0] = t * axis.x * axis.y + s * axis.z;
        m.entry[1][1] = c + t * axis.y * axis.y;
        m.entry[1][2] = t * axis.y * axis.z - s * axis.x;
        m.entry[2][0] = t * axis.x * axis.z - s * axis.y;
        m.entry[2][1] = t * axis.y * axis.z + s * axis.x;
        m.entry[2][2] = c + t * axis.z * axis.z;
        return m;
    }

    RE::NiMatrix3 Between(const RE::NiPoint3& from, const RE::NiPoint3& to)
    {
        const float fromLength = from.Length();
        const float toLength = to.Length();
        if (fromLength < 1e-6F || toLength < 1e-6F) {
            return {};
        }
        const auto  a = from / fromLength;
        const auto  b = to / toLength;
        const float cosine = std::clamp(a.Dot(b), -1.0F, 1.0F);
        auto        axis = a.Cross(b);
        const float sine = axis.Length();
        if (sine < 1e-6F) {
            if (cosine > 0.0F) {
                return {};
            }
            // Opposite: any axis at right angles will do.
            axis = std::fabs(a.x) < 0.9F ? a.Cross({ 1.0F, 0.0F, 0.0F }) : a.Cross({ 0.0F, 1.0F, 0.0F });
            return AxisAngle(axis / axis.Length(), std::numbers::pi_v<float>);
        }
        return AxisAngle(axis / sine, std::atan2(sine, cosine));
    }

    RE::NiMatrix3 Slerp(const RE::NiMatrix3& a, const RE::NiMatrix3& b, float t)
    {
        if (t <= 0.0F) {
            return a;
        }
        if (t >= 1.0F) {
            return b;
        }
        const auto qa = ToQuaternion(a);
        auto       qb = ToQuaternion(b);
        float      dot = qa.w * qb.w + qa.x * qb.x + qa.y * qb.y + qa.z * qb.z;
        if (dot < 0.0F) {
            qb = { -qb.w, -qb.x, -qb.y, -qb.z };
            dot = -dot;
        }
        float wa = 1.0F - t;
        float wb = t;
        if (dot < 0.9995F) {
            const float angle = std::acos(std::clamp(dot, -1.0F, 1.0F));
            const float sine = std::sin(angle);
            wa = std::sin((1.0F - t) * angle) / sine;
            wb = std::sin(t * angle) / sine;
        }
        Quaternion q{ wa * qa.w + wb * qb.w, wa * qa.x + wb * qb.x, wa * qa.y + wb * qb.y, wa * qa.z + wb * qb.z };
        const float length = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
        q = { q.w / length, q.x / length, q.y / length, q.z / length };
        return ToMatrix(q);
    }
}
