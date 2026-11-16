#pragma once

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

/// CORS response headers, written in exactly one place (the pre-sending advice in server.cpp).
/// Every header is removed before it is set, so applying this twice to one response is harmless
/// and a header can never appear twice.
namespace Cors
{
    inline const std::string ALLOWED_METHODS         = "GET, POST, PUT, DELETE, HEAD, OPTIONS";
    inline const std::string DEFAULT_ALLOWED_HEADERS = "Content-Type, Authorization, Accept, X-Requested-With";
    inline const std::string EXPOSED_HEADERS         = "Retry-After, Content-Length";
    inline const std::string PREFLIGHT_MAX_AGE       = "7200";  // browsers cap this at 2 hours anyway

    // Drogon stores headers in a map keyed by the lower-cased name, so addHeader
    // replaces any existing value instead of appending a second one. Removing
    // first is therefore unnecessary; this saves one map operation per header.
    inline void set(const drogon::HttpResponsePtr& resp, const std::string& key, const std::string& value)
    {
        resp->addHeader(key, value);
    }

    namespace detail
    {
        inline bool isTokenChar(char c)
        {
            const unsigned char uc = static_cast<unsigned char>(c);
            if (std::isalnum(uc) != 0)
            {
                return true;
            }
            switch (uc)
            {
                case '!':
                case '#':
                case '$':
                case '%':
                case '&':
                case '\'':
                case '*':
                case '+':
                case '-':
                case '.':
                case '^':
                case '_':
                case '`':
                case '|':
                case '~':
                    return true;
                default:
                    return false;
            }
        }

        inline bool iequals(const std::string& a, const std::string& b)
        {
            if (a.size() != b.size())
            {
                return false;
            }
            return std::equal(a.begin(), a.end(), b.begin(),
                [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
        }

        // Append `value` to the Vary response header without duplicating an
        // existing (case-insensitive) token. Other code may have set Vary first.
        inline void addToVary(const drogon::HttpResponsePtr& resp, const std::string& value)
        {
            std::string existing;
            bool        found = false;
            for (const auto& kv : resp->headers())
            {
                if (iequals(kv.first, "Vary"))
                {
                    existing = kv.second;
                    found    = true;
                    break;
                }
            }

            auto contains = [&value](const std::string& headerValue)
            {
                std::string token;
                for (char c : headerValue + ",")
                {
                    if (c == ',')
                    {
                        const std::string::size_type first = token.find_first_not_of(" \t");
                        if (first != std::string::npos)
                        {
                            const std::string::size_type last = token.find_last_not_of(" \t");
                            if (iequals(token.substr(first, last - first + 1), value))
                            {
                                return true;
                            }
                        }
                        token.clear();
                    }
                    else
                    {
                        token.push_back(c);
                    }
                }
                return false;
            };

            if (found && contains(existing))
            {
                return;
            }
            if (found && !existing.empty())
            {
                resp->addHeader("Vary", existing + ", " + value);
            }
            else
            {
                resp->addHeader("Vary", value);
            }
        }

        // Validate and normalise an Access-Control-Request-Headers value.
        // Returns an empty string when nothing valid remains (or the input is
        // too large), so the caller can fall back to DEFAULT_ALLOWED_HEADERS.
        inline std::string sanitizeRequestedHeaders(const std::string& requested)
        {
            if (requested.empty() || requested.size() > 1024)
            {
                return {};
            }

            std::string result;
            std::string token;
            auto flush = [&]
            {
                const std::string::size_type first = token.find_first_not_of(" \t");
                if (first == std::string::npos)
                {
                    token.clear();
                    return;
                }
                const std::string::size_type last    = token.find_last_not_of(" \t");
                const std::string            trimmed = token.substr(first, last - first + 1);
                token.clear();

                if (trimmed.empty())
                {
                    return;
                }
                if (!std::ranges::all_of(trimmed, isTokenChar))
                {
                    return;
                }
                if (!result.empty())
                {
                    result += ", ";
                }
                result += trimmed;
            };

            for (char c : requested)
            {
                if (c == ',')
                {
                    flush();
                }
                else
                {
                    token.push_back(c);
                }
            }
            flush();
            return result;
        }
    }  // namespace detail

    inline void apply(const drogon::HttpRequestPtr& req, const drogon::HttpResponsePtr& resp)
    {
        // Cors::apply is the only writer; once its marker header is present the
        // response is already complete, so there is nothing more to do.
        const auto& existingHeaders = resp->headers();
        const auto  originIt        = existingHeaders.find("access-control-allow-origin");
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
