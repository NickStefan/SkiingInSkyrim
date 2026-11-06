#pragma once

#include <string_view>

namespace SkiingInSkyrim
{
    [[nodiscard]] bool IsSkiingEnabled() noexcept;
    void SetSkiingEnabled(bool enabled, std::string_view source);
}
