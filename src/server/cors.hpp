#pragma once

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>

#include <string>
#include <string_view>

#include "server/cors_tokens.hpp"

/// CORS response headers, written in exactly one place (the pre-sending advice in server.cpp).
/// Drogon stores headers in a map keyed by the lower-cased name, and addHeader replaces an
/// existing value (HttpResponseImpl.h:194-210), so a header can never appear twice.
namespace Cors
{
    namespace detail = CorsTokens;

    inline const std::string& allowedMethods()
    {
        static const std::string value = "GET, POST, PUT, DELETE, HEAD, OPTIONS";
        return value;
    }

    inline const std::string& defaultAllowedHeaders()
    {
        static const std::string value = "Content-Type, Authorization, Accept, X-Requested-With";
        return value;
    }

    inline const std::string& exposedHeaders()
    {
        static const std::string value = "Retry-After, Content-Length";
        return value;
    }

    inline const std::string& preflightMaxAge()
    {
        static const std::string value = "7200";  // browsers cap this at 2 hours anyway
        return value;
    }

    inline void addToVary(const drogon::HttpResponsePtr& resp, std::string_view value)
    {
        const std::string& existing = resp->getHeader("vary");
        const std::string  merged   = detail::mergeVary(existing, value);
        if (merged != existing)
        {
            resp->addHeader("Vary", merged);
        }
    }

    inline void apply(const drogon::HttpRequestPtr& req, const drogon::HttpResponsePtr& resp)
    {
        static const std::string originKey = "access-control-allow-origin";
        const auto&              headers   = resp->headers();
        const auto               originIt  = headers.find(originKey);
        if (originIt != headers.end() && originIt->second == "*")
        {
            return;
        }

        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->addHeader("Access-Control-Allow-Methods", allowedMethods());
        resp->addHeader("Access-Control-Expose-Headers", exposedHeaders());

        const std::string& requested = req->getHeader("Access-Control-Request-Headers");

        const std::string* allowedHeaders = &defaultAllowedHeaders();
        std::string echoed;
        if (req->method() == drogon::Options)
        {
            echoed = detail::sanitizeRequestedHeaders(requested);
            if (!echoed.empty())
            {
                allowedHeaders = &echoed;
            }
            resp->addHeader("Access-Control-Max-Age", preflightMaxAge());
            addToVary(resp, "Access-Control-Request-Headers");
        }
        resp->addHeader("Access-Control-Allow-Headers", *allowedHeaders);
    }
}  // namespace Cors
