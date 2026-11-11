#include "core/Flow.h"

#include <algorithm>
#include <cmath>

namespace SkiingInSkyrim::Core
{
    bool FlowDescend(FlowState& state, float metres, const FlowSettings& settings) noexcept
    {
        if (state.Full() || !(metres > 0.0F) || !std::isfinite(metres)) {
            return false;
        }
        state.descended += metres;
        if (state.descended < std::max(settings.fillMetres, 1.0F)) {
            return false;
        }
        state.descended = 0.0F;
        state.reserve = std::max(settings.reserveMetres, 1.0F);
        return true;
    }

    void FlowFall(FlowState& state) noexcept
    {
        if (!state.Full()) {
            state.descended = 0.0F;
        }
    }

    bool FlowAbsorb(FlowState& state, float metres) noexcept
    {
        if (!state.Full()) {
            return false;
        }
        state.reserve = std::max(0.0F, state.reserve - std::max(0.0F, metres));
        return true;
    }

    float FlowShown(const FlowState& state, const FlowSettings& settings) noexcept
    {
        if (state.Full()) {
            return std::clamp(state.reserve / std::max(settings.reserveMetres, 1.0F), 0.0F, 1.0F);
        }
        return std::clamp(state.descended / std::max(settings.fillMetres, 1.0F), 0.0F, 1.0F);
    }
}
