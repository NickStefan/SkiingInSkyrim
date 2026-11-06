#pragma once

namespace SkiingInSkyrim::Telemetry
{
    // Pass-through hook on PlayerCharacter::Update that logs movement state while skiing mode is on.
    void Install();
}
