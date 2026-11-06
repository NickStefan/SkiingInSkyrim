#include "core/Ini.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>

namespace SkiingInSkyrim::Core
{
    namespace
    {
        std::string_view Trim(std::string_view text)
        {
            const auto isSpace = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
            while (!text.empty() && isSpace(text.front())) {
                text.remove_prefix(1);
            }
            while (!text.empty() && isSpace(text.back())) {
                text.remove_suffix(1);
            }
            return text;
        }

        std::string Lower(std::string_view text)
        {
            std::string result(text);
            std::ranges::transform(result, result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return result;
        }

        std::string MakeKey(std::string_view section, std::string_view key)
        {
            return Lower(section) + '.' + Lower(key);
        }

        template <class T>
        std::optional<T> ParseNumber(std::string_view text)
        {
            T value{};
            const auto* end = text.data() + text.size();
            const auto [ptr, error] = std::from_chars(text.data(), end, value);
            if (error != std::errc{} || ptr != end) {
                return std::nullopt;
            }
            return value;
        }
    }

    Ini Ini::Parse(std::string_view text)
    {
        Ini ini;
        std::string section;
        while (!text.empty()) {
            const auto newline = text.find('\n');
            auto line = Trim(text.substr(0, newline));
            text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);

            if (line.empty() || line.front() == ';' || line.front() == '#') {
                continue;
            }
            if (line.front() == '[') {
                const auto close = line.find(']');
                if (close != std::string_view::npos) {
                    section = std::string(Trim(line.substr(1, close - 1)));
                }
                continue;
            }
            const auto equals = line.find('=');
            if (equals == std::string_view::npos) {
                continue;
            }
            auto value = line.substr(equals + 1);
            if (const auto comment = value.find_first_of(";#"); comment != std::string_view::npos) {
                value = value.substr(0, comment);
            }
            ini.values_[MakeKey(section, Trim(line.substr(0, equals)))] = std::string(Trim(value));
        }
        return ini;
    }

    std::optional<std::string> Ini::Get(std::string_view section, std::string_view key) const
    {
        const auto found = values_.find(MakeKey(section, key));
        return found == values_.end() ? std::nullopt : std::optional{ found->second };
    }

    std::optional<float> Ini::GetFloat(std::string_view section, std::string_view key) const
    {
        const auto text = Get(section, key);
        if (!text) {
            return std::nullopt;
        }
        const auto value = ParseNumber<float>(*text);
        return value && std::isfinite(*value) ? value : std::nullopt;
    }

    std::optional<long long> Ini::GetInt(std::string_view section, std::string_view key) const
    {
        const auto text = Get(section, key);
        return text ? ParseNumber<long long>(*text) : std::nullopt;
    }
}
