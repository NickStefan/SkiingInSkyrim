#include "plugin/Tdm.h"

#include "TrueDirectionalMovement/src/TrueDirectionalMovementAPI.h"

namespace SkiingInSkyrim::Tdm
{
    namespace
    {
        using TDM_API::APIResult;
        using TDM_API::InterfaceVersion;

        constexpr float kRetrySeconds = 2.0F;

        // The API pointers and retry timer are only touched on the main thread; the rest is read by the menu.
        struct State
        {
            TDM_API::IVTDM2*         api{ nullptr };
            TDM_API::IVTDM3*         apiV3{ nullptr };
            float                    retryIn{ 0.0F };
            std::atomic<Connection>  connection{ Connection::NotInstalled };
            std::atomic<int>         apiVersion{ 0 };
            std::atomic<bool>        yawControl{ false };
            std::atomic<bool>        yawDenied{ false };
            std::atomic<const char*> status{ "not installed" };
            std::atomic<const char*> lastResult{ "" };
        };

        State g_state;

        const char* ResultName(APIResult result) noexcept
        {
            switch (result) {
            case APIResult::OK:           return "OK";
            case APIResult::NotOwner:     return "NotOwner";
            case APIResult::MustKeep:     return "MustKeep (TDM needs it right now)";
            case APIResult::AlreadyGiven: return "AlreadyGiven";
            case APIResult::AlreadyTaken: return "AlreadyTaken (another mod holds it)";
            case APIResult::BadThread:    return "BadThread";
            default:                      return "Unknown";
            }
        }
    }

    void Connect()
    {
        if (!GetModuleHandleA("TrueDirectionalMovement.dll")) {
            spdlog::info("True Directional Movement not installed");
            return;
        }
        // V2 added yaw control, which is the minimum we need.
        for (const auto version : { InterfaceVersion::V5, InterfaceVersion::V4, InterfaceVersion::V3, InterfaceVersion::V2 }) {
            if (auto* api = TDM_API::RequestPluginAPI(version)) {
                g_state.api = static_cast<TDM_API::IVTDM2*>(api);
                if (version >= InterfaceVersion::V3) {
                    g_state.apiV3 = static_cast<TDM_API::IVTDM3*>(api);
                }
                const int number = static_cast<int>(version) + 1;
                g_state.apiVersion = number;
                g_state.connection = Connection::Connected;
                g_state.status = "idle";
                spdlog::info("True Directional Movement API v{} connected", number);
                return;
            }
        }
        g_state.connection = Connection::NoCompatibleApi;
        g_state.status = "no compatible API";
        spdlog::warn("True Directional Movement is installed but offers no API with yaw control; it may fight skiing");
    }

    void Update(bool wantYawControl)
    {
        auto& state = g_state;
        if (!state.api) {
            return;
        }
        const auto handle = SKSE::GetPluginHandle();

        if (!wantYawControl) {
            if (state.yawControl) {
                const auto result = state.api->ReleaseYawControl(handle);
                spdlog::info("released TDM yaw control: {}", ResultName(result));
                state.lastResult = ResultName(result);
                state.yawControl = false;
                state.status = "idle";
            }
            state.retryIn = 0.0F;
            state.yawDenied = false;
            return;
        }
        if (state.yawControl) {
            return;
        }
        if (state.retryIn > 0.0F) {
            state.retryIn -= RE::GetSecondsSinceLastFrame();
            return;
        }

        const auto result = state.api->RequestYawControl(handle, 0.0F);
        state.lastResult = ResultName(result);
        if (result == APIResult::OK || result == APIResult::AlreadyGiven) {
            state.yawControl = true;
            state.yawDenied = false;
            state.status = "yaw";
            spdlog::info("took TDM yaw control: {}", ResultName(result));
        } else {
            state.retryIn = kRetrySeconds;
            state.yawDenied = true;
            state.status = "yaw denied";
            spdlog::warn("TDM denied yaw control ({}); retrying in {}s", ResultName(result), kRetrySeconds);
        }
    }

    Info GetInfo() noexcept
    {
        return { g_state.connection.load(), g_state.apiVersion.load(), g_state.yawControl.load(), g_state.yawDenied.load(),
            g_state.lastResult.load() };
    }

    bool HasYawControl() noexcept { return g_state.yawControl; }
    const char* Status() noexcept { return g_state.status; }

    std::optional<RE::NiPoint2> ActualMovementInput()
    {
        if (!g_state.apiV3) {
            return std::nullopt;
        }
        return g_state.apiV3->GetActualMovementInput();
    }

    bool SetPlayerYaw(float yaw)
    {
        return g_state.yawControl && g_state.api->SetPlayerYaw(SKSE::GetPluginHandle(), yaw) == APIResult::OK;
    }
}
