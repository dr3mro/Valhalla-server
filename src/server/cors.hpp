#pragma once

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>
#include <drogon/utils/Utilities.h>

#include <algorithm>
#include <ranges>
#include <string>
#include <string_view>

/// CORS response headers, written in exactly one place (the pre-sending advice in server.cpp).
/// Every header is set with addHeader, which replaces any existing value in Drogon's header
/// map, so a header can never appear twice.
namespace Cors
{
    inline const std::string ALLOWED_METHODS         = "GET, POST, PUT, DELETE, HEAD, OPTIONS";
    inline const std::string DEFAULT_ALLOWED_HEADERS = "Content-Type, Authorization, Accept, X-Requested-With";
    inline const std::string EXPOSED_HEADERS         = "Retry-After, Content-Length";
    inline const std::string PREFLIGHT_MAX_AGE       = "7200";  // browsers cap this at 2 hours anyway

    inline void set(const drogon::HttpResponsePtr& resp, const std::string& key, const std::string& value)
    {
        // Drogon stores headers in a map keyed by the lower-cased name, so addHeader
        // replaces any existing value instead of appending a second one.
        resp->addHeader(key, value);
    }

    namespace detail
    {
        constexpr std::string_view tokenPunctuation = "!#$%&'*+-.^_`|~";

        constexpr bool isTokenChar(char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                   tokenPunctuation.find(c) != std::string_view::npos;
        }

        inline bool asciiLowerEqual(char a, char b)
        {
            auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c; };
            return lower(a) == lower(b);
        }

        inline bool iequals(std::string_view a, std::string_view b)
        {
            return a.size() == b.size() && std::ranges::equal(a, b, asciiLowerEqual);
        }

        inline std::vector<std::string_view> splitTokens(std::string_view value)
        {
            return drogon::utils::splitStringView(value, ",", /*trimValues=*/true, /*acceptEmptyString=*/false);
        }

        // Validate and normalise an Access-Control-Request-Headers value.
        // Returns an empty string when nothing valid remains (or the input is
        // too large), so the caller can fall back to DEFAULT_ALLOWED_HEADERS.
        inline std::string sanitizeRequestedHeaders(std::string_view requested)
        {
            if (requested.empty() || requested.size() > 1024)
            {
                return {};
            }

            std::string result;
            for (std::string_view token : splitTokens(requested))
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

        // Append `value` to the Vary response header without duplicating an
        // existing (case-insensitive) token. Other code may have set Vary first.
        inline void addToVary(const drogon::HttpResponsePtr& resp, std::string_view value)
        {
            const auto& headers = resp->headers();
            const auto  it      = std::ranges::find_if(headers, [](const auto& kv) { return iequals(kv.first, "Vary"); });

            std::string combined;
            if (it != headers.end())
            {
                bool present = false;
                for (std::string_view token : splitTokens(it->second))
                {
                    if (iequals(token, value))
                    {
                        present = true;
                        break;
                    }
                }
                if (present)
                {
                    return;
                }
                for (std::string_view token : splitTokens(it->second))
                {
                    if (!combined.empty())
                    {
                        combined += ", ";
                    }
                    combined.append(token);
                }
            }

            if (!combined.empty())
            {
                combined += ", ";
            }
            combined.append(value);
            resp->addHeader("Vary", combined);
        }
    }  // namespace detail

    inline void apply(const drogon::HttpRequestPtr& req, const drogon::HttpResponsePtr& resp)
    {
        const auto& existingHeaders = resp->headers();
        const auto  originIt        = std::ranges::find_if(existingHeaders, [](const auto& kv) { return detail::iequals(kv.first, "access-control-allow-origin"); });
        if (originIt != existingHeaders.end() && originIt->second == "*")
        {
            return;
        }

        set(resp, "Access-Control-Allow-Origin", "*");
        set(resp, "Access-Control-Allow-Methods", ALLOWED_METHODS);
        set(resp, "Access-Control-Expose-Headers", EXPOSED_HEADERS);

        std::string allowedHeaders = DEFAULT_ALLOWED_HEADERS;
        if (req->method() == drogon::Options)
        {
            // Preflight only: allow whatever the browser says it is about to send,
            // after validating the token list and bounding its size.
            const std::string requested = req->getHeader("Access-Control-Request-Headers");
            const std::string echoed    = detail::sanitizeRequestedHeaders(requested);
            if (!echoed.empty())
            {
                allowedHeaders = echoed;
            }
            set(resp, "Access-Control-Max-Age", PREFLIGHT_MAX_AGE);
            detail::addToVary(resp, "Access-Control-Request-Headers");
        }
        set(resp, "Access-Control-Allow-Headers", allowedHeaders);
    }
}  // namespace Cors