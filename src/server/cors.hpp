#pragma once

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>

#include <string>

/// CORS response headers, written in exactly one place (the pre-sending advice in server.cpp).
/// Every header is removed before it is set, so applying this twice to one response is harmless
/// and a header can never appear twice.
namespace Cors
{
    inline constexpr const char* ALLOWED_METHODS         = "GET, POST, PUT, DELETE, HEAD, OPTIONS";
    inline constexpr const char* DEFAULT_ALLOWED_HEADERS = "Content-Type, Authorization, Accept, X-Requested-With";
    inline constexpr const char* EXPOSED_HEADERS         = "Retry-After, Content-Length";
    inline constexpr const char* PREFLIGHT_MAX_AGE       = "7200";  // browsers cap this at 2 hours anyway

    inline void set(const drogon::HttpResponsePtr& resp, const std::string& key, const std::string& value)
    {
        resp->removeHeader(key);
        resp->addHeader(key, value);
    }

    inline void apply(const drogon::HttpRequestPtr& req, const drogon::HttpResponsePtr& resp)
    {
        set(resp, "Access-Control-Allow-Origin", "*");
        set(resp, "Access-Control-Allow-Methods", ALLOWED_METHODS);
        set(resp, "Access-Control-Expose-Headers", EXPOSED_HEADERS);

        std::string allowedHeaders = DEFAULT_ALLOWED_HEADERS;
        if (req->method() == drogon::Options)
        {
            // Preflight only: allow whatever the browser says it is about to send.
            const std::string& requested = req->getHeader("Access-Control-Request-Headers");
            if (!requested.empty())
            {
                allowedHeaders = requested;
            }
            set(resp, "Access-Control-Max-Age", PREFLIGHT_MAX_AGE);
        }
        set(resp, "Access-Control-Allow-Headers", allowedHeaders);
    }
}  // namespace Cors
