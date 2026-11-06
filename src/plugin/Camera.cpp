#include "plugin/Camera.h"

#include "core/CameraFollow.h"
#include "plugin/Settings.h"

#include <numbers>

namespace SkiingInSkyrim::Camera
{
    namespace
    {
        constexpr float kDegrees = 180.0F / std::numbers::pi_v<float>;

        float WrapSigned(float angle) noexcept
        {
            constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;
            angle = std::fmod(angle + std::numbers::pi_v<float>, kTwoPi);
            if (angle < 0.0F) {
                angle += kTwoPi;
            }
            return angle - std::numbers::pi_v<float>;
        }

        // Main thread only, except the atomics.
        struct State
        {
            Core::CameraFollowState  follow;
            std::optional<float>     observed;
            std::atomic<const char*> status{ "idle" };
            std::atomic<bool>        smoothCam{ false };
            std::atomic<float>       yawDegrees{ 0.0F };
            std::atomic<float>       lookDegrees{ 0.0F };
        };

        State g_state;

        void SetStatus(const char* status)
        {
            if (g_state.status.exchange(status) != status) {
                spdlog::info("chase camera: {}", status);
            }
        }

        // The third-person state if the camera is in it and lets the camera orbit the player.
        RE::ThirdPersonState* ThirdPerson(const char** whyNot)
        {
            auto* camera = RE::PlayerCamera::GetSingleton();
            if (!camera || !camera->IsCurrentState(RE::CameraState::kThirdPerson, RE::CameraState::kVRThirdPerson)) {
                *whyNot = "not in third person";
                return nullptr;
            }
            auto* state = static_cast<RE::ThirdPersonState*>(camera->currentState.get());
            if (!state || !state->freeRotationEnabled) {
                *whyNot = "camera free rotation off";
                return nullptr;
            }
            return state;
        }
    }

    void Connect()
    {
        const bool smoothCam = GetModuleHandleA("SmoothCam.dll") || GetModuleHandleA("SmoothCamAE.dll");
        g_state.smoothCam = smoothCam;
        spdlog::info("SmoothCam {}", smoothCam ? "installed" : "not installed");
    }

    void Observe(RE::PlayerCharacter* player)
    {
        const char* whyNot = "";
        const auto* state = ThirdPerson(&whyNot);
        g_state.observed = state ? std::optional{ player->data.angle.z + state->freeRotation.x } : std::nullopt;
    }

    void Apply(RE::PlayerCharacter* player, float delta, float skiYaw, Core::Vec2 velocity,
        std::optional<float> heading)
    {
        const auto settings = Settings::Camera();
        if (!settings.enabled) {
            g_state.follow = {};
            SetStatus("off");
            return;
        }
        const char* whyNot = "";
        auto*       state = ThirdPerson(&whyNot);
        if (!state || !g_state.observed) {
            g_state.follow = {};
            SetStatus(state ? "waiting" : whyNot);
            return;
        }
        const float yaw = Core::UpdateCameraFollow(g_state.follow, settings, delta, *g_state.observed, skiYaw, velocity, heading);
        state->freeRotation.x = WrapSigned(yaw - player->data.angle.z);
        g_state.yawDegrees = g_state.follow.yaw * kDegrees;
        g_state.lookDegrees = g_state.follow.look * kDegrees;
        SetStatus("following travel");
    }

    void Reset()
    {
        g_state.follow = {};
        g_state.observed.reset();
        SetStatus("idle");
    }

    Info GetInfo() noexcept
    {
        return { g_state.status, g_state.smoothCam, g_state.yawDegrees, g_state.lookDegrees };
    }
}
