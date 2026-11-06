#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace SkiingInSkyrim::Core
{
    // Minimal INI reader: [Section] headers, key = value lines, ';' or '#' comments.
    // Section and key lookups are case-insensitive.
    class Ini
    {
    public:
        static Ini Parse(std::string_view text);

        [[nodiscard]] std::optional<std::string> Get(std::string_view section, std::string_view key) const;
        [[nodiscard]] std::optional<float>       GetFloat(std::string_view section, std::string_view key) const;
        [[nodiscard]] std::optional<long long>   GetInt(std::string_view section, std::string_view key) const;

    private:
        std::map<std::string, std::string> values_;  // "section.key", lowercased
    };
}
