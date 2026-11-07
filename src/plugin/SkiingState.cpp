#include "plugin/SkiingState.h"

#include "plugin/Settings.h"

#include <atomic>

namespace SkiingInSkyrim
{
    namespace
    {
        std::atomic<bool> g_skisWorn{ false };
    }

    bool IsSkiingEnabled() { return Settings::ModEnabled() && AreSkisWorn(); }

    bool AreSkisWorn() noexcept { return g_skisWorn.load(std::memory_order_relaxed); }

    void SetSkisWorn(bool worn) noexcept { g_skisWorn.store(worn, std::memory_order_relaxed); }
}
