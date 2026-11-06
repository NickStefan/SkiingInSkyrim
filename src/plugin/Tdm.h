#pragma once

namespace SkiingInSkyrim::Tdm
{
    // Looks up True Directional Movement's API; call after all plugins have loaded.
    void Connect();

    // Main thread, every frame: takes TDM's control of the player's yaw while skiing and returns it afterwards.
    void Update(bool wantYawControl);

    enum class Connection
    {
        NotInstalled,
        NoCompatibleApi,
        Connected
    };

    // Safe to read from any thread (the menu renders off the main thread).
    struct Info
    {
        Connection  connection{ Connection::NotInstalled };
        int         apiVersion{ 0 };
        bool        yawControl{ false };
        bool        yawDenied{ false };    // the last request was refused; retrying
        const char* lastResult{ "" };  // result of the last yaw request/release, empty if none
    };

    [[nodiscard]] Info        GetInfo() noexcept;
    [[nodiscard]] bool        HasYawControl() noexcept;
    [[nodiscard]] const char* Status() noexcept;

    // Movement input before TDM rewrote it (TDM API v3+).
    [[nodiscard]] std::optional<RE::NiPoint2> ActualMovementInput();

    // Sets the player's yaw through TDM; only works while HasYawControl().
    bool SetPlayerYaw(float yaw);
}
