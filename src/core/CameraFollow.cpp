#include "core/CameraFollow.h"

#include <algorithm>
#include <numbers>

namespace SkiingInSkyrim::Core
{
    namespace
    {
        constexpr float kPi = std::numbers::pi_v<float>;
        constexpr float kRadians = kPi / 180.0F;

        float WrapSigned(float radians) noexcept
        {
            radians = std::fmod(radians + kPi, 2.0F * kPi);
            return (radians < 0.0F ? radians + 2.0F * kPi : radians) - kPi;
        }

        float WrapPositive(float radians) noexcept
        {
            radians = std::fmod(radians, 2.0F * kPi);
            return radians < 0.0F ? radians + 2.0F * kPi : radians;
        }

        float Smoothing(float deltaSeconds, float timeConstant) noexcept
        {
            return timeConstant > 0.0F ? 1.0F - std::exp(-deltaSeconds / timeConstant) : 1.0F;
        }
    }

    float UpdateCameraFollow(CameraFollowState& state, const CameraFollowSettings& settings, float deltaSeconds,
        float observedYaw, Vec2 velocity) noexcept
    {
        const float dt = std::max(0.0F, deltaSeconds);
        if (!state.started) {
            state = {};
            state.started = true;
            state.yaw = observedYaw;
            state.lastWritten = observedYaw;
        }

        const float looked = WrapSigned(observedYaw - state.lastWritten);
        if (std::fabs(looked) > 1e-4F) {
            state.look = WrapSigned(state.look + looked);
            state.sinceLook = 0.0F;
        } else {
            state.sinceLook += dt;
            if (state.sinceLook > settings.lookHoldSeconds) {
                state.look *= 1.0F - Smoothing(dt, settings.lookReturnSeconds);
            }
        }

        // Average the direction as a vector, so swings either side cancel instead of wrapping.
        const float speed = Length(velocity);
        if (speed > 1e-4F) {
            const float blend = Smoothing(dt, settings.travelSmoothingSeconds);
            state.travel.x += (velocity.x / speed - state.travel.x) * blend;
            state.travel.y += (velocity.y / speed - state.travel.y) * blend;
        }

        const float span = settings.travelFullSpeed - settings.travelMinSpeed;
        const float follow = span > 0.0F ? std::clamp((speed - settings.travelMinSpeed) / span, 0.0F, 1.0F) :
                                           (speed >= settings.travelMinSpeed ? 1.0F : 0.0F);
        if (follow > 0.0F && Length(state.travel) > 1e-3F) {
            // Beyond the lazy zone the camera follows at the normal rate; within it, only slowly.
            const float offset = WrapSigned(std::atan2(state.travel.x, state.travel.y) - state.yaw);
            const float zone = std::max(0.0F, settings.lazyZoneDegrees) * kRadians;
            const float inside = std::clamp(offset, -zone, zone);
            const float outside = offset - inside;
            state.yaw = WrapPositive(state.yaw + follow * (outside * Smoothing(dt, settings.followSeconds) +
                                                              inside * Smoothing(dt, settings.recenterSeconds)));
        }

        state.lastWritten = WrapPositive(state.yaw + state.look);
        return state.lastWritten;
    }
}
