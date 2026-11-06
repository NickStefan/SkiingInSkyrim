#pragma once

namespace SkiingInSkyrim::Input
{
    // Raw player input, read from input events before movement handlers (and mods like TDM) reinterpret it.
    struct Snapshot
    {
        bool  forward{ false };
        bool  back{ false };
        bool  left{ false };
        bool  right{ false };
        float stickX{ 0.0F };
        float stickY{ 0.0F };
        float triggerLeft{ 0.0F };  // 0..1
        float triggerRight{ 0.0F };
        bool  mouseLeft{ false };
        bool  mouseRight{ false };
        // Seconds since the last keyboard movement event / left stick event; negative if never seen.
        float keyAge{ -1.0F };
        float stickAge{ -1.0F };
    };

    bool     Register();
    Snapshot Get();

    // Hooks the attack/block handler so the edge buttons don't swing weapons while skiing.
    void Install();

    // Toggle-key capture for the settings menu. While capturing, the next button press (other than
    // Escape, which cancels, or a left click) becomes the toggle key.
    void BeginCapture() noexcept;
    void CancelCapture() noexcept;
    bool IsCapturing() noexcept;

    // Input seen by SKSE Menu Framework before it pauses the game; returns true to consume the event.
    bool OnMenuFrameworkInput(RE::InputEvent* event);
}
