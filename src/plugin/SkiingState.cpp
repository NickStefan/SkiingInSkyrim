#include "plugin/SkiingState.h"

#include <atomic>

namespace SkiingInSkyrim
{
    namespace
    {
        std::atomic<bool> g_enabled{ false };
    }

    bool IsSkiingEnabled() noexcept
    {
        return g_enabled.load(std::memory_order_relaxed);
    }

    void SetSkiingEnabled(bool enabled, std::string_view source)
    {
        if (g_enabled.exchange(enabled, std::memory_order_relaxed) != enabled) {
            spdlog::info("skiing mode {} ({})", enabled ? "enabled" : "disabled", source);
        }
    }
}
