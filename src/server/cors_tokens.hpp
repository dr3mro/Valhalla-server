#pragma once

#include <algorithm>
#include <ranges>
#include <string>
#include <string_view>

/// Drogon-free helpers for parsing and merging CORS header token lists.
namespace CorsTokens
{
    constexpr std::string_view tokenPunctuation = "!#$%&'*+-.^_`|~";

    constexpr bool isTokenChar(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || tokenPunctuation.find(c) != std::string_view::npos;
    }

    inline std::string_view trim(std::string_view value)
    {
        const std::size_t first = value.find_first_not_of(" \t");
        if (first == std::string_view::npos)
        {
            return {};
        }
        const std::size_t last = value.find_last_not_of(" \t");
        return value.substr(first, last - first + 1);
    }

    inline auto tokens(std::string_view value)
    {
        return value | std::views::split(',') |
               std::views::transform(
                   [](auto range)
                   {
                       if (range.begin() == range.end())
                       {
                           return std::string_view{};
                       }
                       return trim(std::string_view(&*range.begin(), std::ranges::distance(range)));
                   }) |
               std::views::filter([](std::string_view token) { return !token.empty(); });
    }

    inline bool iequals(std::string_view a, std::string_view b)
    {
        auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c; };
        return a.size() == b.size() && std::ranges::equal(a, b, [lower](char x, char y) { return lower(x) == lower(y); });
    }

    inline std::string sanitizeRequestedHeaders(std::string_view requested)
    {
        if (requested.empty() || requested.size() > 1024)
        {
            return {};
        }

        std::string result;
        for (std::string_view token : tokens(requested))
        {
            if (!std::ranges::all_of(token, isTokenChar))
            {
                continue;
            }
            if (!result.empty())
            {
                result += ", ";
            }
            result.append(token);
        }
        return result;
    }

    inline std::string mergeVary(std::string_view existing, std::string_view value)
    {
        const bool hasWildcard = std::ranges::any_of(tokens(existing), [](std::string_view token) { return token == "*"; });
        if (hasWildcard)
        {
            return std::string(existing);
        }

        const bool present = std::ranges::any_of(tokens(existing), [&](std::string_view token) { return iequals(token, value); });
        if (present)
        {
            return std::string(existing);
        }

        std::string combined;
        for (std::string_view token : tokens(existing))
        {
            if (!combined.empty())
            {
                combined += ", ";
            }
            combined.append(token);
        }
        if (!combined.empty())
        {
            combined += ", ";
        }
        combined.append(value);
        if (existing.empty())
        {
            return std::string(value);
        }
        return combined;
    }
}  // namespace CorsTokens
