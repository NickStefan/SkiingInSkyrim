#pragma once

namespace SkiingInSkyrim::FlowBar
{
    // SKSE Menu Framework HUD callback (render thread). Draws the flow bar in the top right corner when flow is
    // on, the skis are worn and no menu is open. Independent of the debug overlay.
    void __stdcall Render();
}
