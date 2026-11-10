#include "core/TumbleCamera.h"

#include <algorithm>

namespace SkiingInSkyrim::Core
{
    namespace
    {
        float Blend(float deltaSeconds, float seconds) noexcept
        {
            return seconds > 0.0F ? 1.0F - std::exp(-std::max(0.0F, deltaSeconds) / seconds) : 1.0F;
        }

        Vec3 Toward(Vec3 from, Vec3 to, float t) noexcept
        {
            return { from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t, from.z + (to.z - from.z) * t };
        }
    }

    void StartTumbleCamera(TumbleCameraState& state, Vec3 camera, Vec3 body) noexcept
    {
        const Vec3 offset{ camera.x - body.x, camera.y - body.y, camera.z - body.z };
        state = { .started = true,
            .camera = camera,
            .offset = offset,
            .look = body,
            .reach = Length(offset),
            .shown = camera };
    }

    void PlaceTumbleCamera(TumbleCameraState& state, const TumbleCameraSettings& settings, float deltaSeconds,
        std::optional<float> clearUnits) noexcept
    {
        const Vec3  out{ state.camera.x - state.look.x, state.camera.y - state.look.y, state.camera.z - state.look.z };
        const float distance = Length(out);
        float       wanted = distance;
        if (clearUnits) {
            wanted = std::min(wanted, std::max(*clearUnits - settings.obstructionMarginUnits, settings.closestUnits));
        }
        if (wanted < state.reach) {
            state.reach = wanted;
        } else {
            state.reach += (wanted - state.reach) * Blend(deltaSeconds, settings.pullOutSeconds);
        }
        const float t = distance > 1e-3F ? std::min(state.reach, distance) / distance : 1.0F;
        state.shown = Toward(state.look, state.camera, t);
    }

    void UpdateTumbleCamera(TumbleCameraState& state, const TumbleCameraSettings& settings, float deltaSeconds,
        Vec3 body, std::optional<float> groundZ) noexcept
    {
        state.seconds += std::max(0.0F, deltaSeconds);
        state.look = Toward(state.look, body, Blend(deltaSeconds, settings.lookSeconds));
        if (state.seconds > settings.freezeSeconds) {
            const Vec3 target{ body.x + state.offset.x, body.y + state.offset.y, body.z + state.offset.z };
            state.camera = Toward(state.camera, target, Blend(deltaSeconds, settings.followSeconds));
        }
        if (groundZ) {
            state.camera.z = std::max(state.camera.z, *groundZ + settings.clearanceUnits);
        }
    }

    LookAngles LookFrom(Vec3 from, Vec3 to) noexcept
    {
        const Vec3 d{ to.x - from.x, to.y - from.y, to.z - from.z };
        return { std::atan2(d.x, d.y), std::atan2(-d.z, HorizontalLength(d)) };
    }
}
