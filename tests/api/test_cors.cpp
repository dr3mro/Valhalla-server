#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>

#include <catch2/catch_test_macros.hpp>
#include <string>

#include "server/cors.hpp"

namespace
{
    drogon::HttpRequestPtr makeRequest(drogon::HttpMethod method, const std::string& requestedHeaders = "")
    {
        auto request = drogon::HttpRequest::newHttpRequest();
        request->setMethod(method);
        if (!requestedHeaders.empty())
        {
            request->addHeader("Access-Control-Request-Headers", requestedHeaders);
        }
        return request;
    }
}  // namespace

TEST_CASE("Cors::apply sets the full header set on a normal response", "[api][cors]")
{
    auto response = drogon::HttpResponse::newHttpResponse();
    Cors::apply(makeRequest(drogon::Get), response);

    CHECK(response->getHeader("Access-Control-Allow-Origin") == "*");
    CHECK(response->getHeader("Access-Control-Allow-Methods").find("PUT") != std::string::npos);
    CHECK(response->getHeader("Access-Control-Allow-Methods").find("DELETE") != std::string::npos);
    CHECK(response->getHeader("Access-Control-Allow-Headers") == Cors::DEFAULT_ALLOWED_HEADERS);
    CHECK(response->getHeader("Access-Control-Expose-Headers").find("Retry-After") != std::string::npos);
    CHECK(response->getHeader("Access-Control-Max-Age").empty());  // preflight only
    CHECK(response->getHeader("Allow").empty());                   // not a CORS header
}

TEST_CASE("Cors::detail helpers are ASCII-only and trim tokens", "[api][cors]")
{
    using Cors::detail::sanitizeRequestedHeaders;

    // tabs and spaces around tokens are trimmed
    CHECK(sanitizeRequestedHeaders("\t content-type ,  x-custom  ") == "content-type, x-custom");
    // empty tokens between commas are dropped
    CHECK(sanitizeRequestedHeaders("content-type,,x-custom") == "content-type, x-custom");
    // a non-ASCII byte makes the whole token invalid
    CHECK(sanitizeRequestedHeaders(std::string("x-c\xc3\xbustom")) == "");
    // a normal ASCII token still validates
    CHECK(sanitizeRequestedHeaders("X-Token_1.2") == "X-Token_1.2");
}

TEST_CASE("Cors::detail addToVary is case-insensitive", "[api][cors]")
{
    auto response = drogon::HttpResponse::newHttpResponse();
    response->addHeader("Vary", "Accept-Encoding, access-control-request-headers");

    Cors::detail::addToVary(response, "Access-Control-Request-Headers");

    CHECK(response->getHeader("Vary") == "Accept-Encoding, access-control-request-headers");
}

TEST_CASE("Cors::apply does not duplicate a pre-existing Allow-Origin", "[api][cors]")
{
    auto response = drogon::HttpResponse::newHttpResponse();
    response->addHeader("Access-Control-Allow-Origin", "*");

    Cors::apply(makeRequest(drogon::Get), response);

    CHECK(response->getHeader("Access-Control-Allow-Origin") == "*");
    CHECK(response->headers().count("access-control-allow-origin") == 1);
}

TEST_CASE("Cors::apply is idempotent: applying it twice changes nothing", "[api][cors]")
{
    auto       response = drogon::HttpResponse::newHttpResponse();
    const auto request  = makeRequest(drogon::Get);

    Cors::apply(request, response);
    const auto once = response->getHeader("Access-Control-Allow-Origin");
    const auto size = response->headers().size();

    Cors::apply(request, response);
    CHECK(response->getHeader("Access-Control-Allow-Origin") == once);
    CHECK(response->headers().size() == size);
}

TEST_CASE("Cors::apply preserves and appends Vary", "[api][cors]")
{
    auto response = drogon::HttpResponse::newHttpResponse();

    SECTION("no existing Vary gets exactly ours")
    {
        Cors::apply(makeRequest(drogon::Options, "content-type"), response);
        CHECK(response->getHeader("Vary") == "Access-Control-Request-Headers");
    }

    SECTION("an existing Vary is preserved and ours is appended")
    {
        response->addHeader("Vary", "Accept-Encoding");
        Cors::apply(makeRequest(drogon::Options, "content-type"), response);
        CHECK(response->getHeader("Vary") == "Accept-Encoding, Access-Control-Request-Headers");
    }

    SECTION("applying twice does not duplicate the token")
    {
        Cors::apply(makeRequest(drogon::Options, "content-type"), response);
        Cors::apply(makeRequest(drogon::Options, "content-type"), response);
        CHECK(response->getHeader("Vary") == "Access-Control-Request-Headers");
    }
}

TEST_CASE("Cors::apply hardens the preflight echo", "[api][cors]")
{
    auto response = drogon::HttpResponse::newHttpResponse();

    SECTION("oversized value falls back to the default")
    {
        const std::string huge(1025, 'a');
        Cors::apply(makeRequest(drogon::Options, huge), response);
        CHECK(response->getHeader("Access-Control-Allow-Headers") == Cors::DEFAULT_ALLOWED_HEADERS);
    }

    SECTION("a value with a space or control character is rejected")
    {
        Cors::apply(makeRequest(drogon::Options, "x bad"), response);
        CHECK(response->getHeader("Access-Control-Allow-Headers") == Cors::DEFAULT_ALLOWED_HEADERS);

        const std::string withControl = std::string("\x01authorization");
        Cors::apply(makeRequest(drogon::Options, withControl), response);
        CHECK(response->getHeader("Access-Control-Allow-Headers") == Cors::DEFAULT_ALLOWED_HEADERS);
    }

    SECTION("mixed valid and invalid tokens keeps only the valid ones")
    {
        Cors::apply(makeRequest(drogon::Options, "content-type, not a token, x-custom"), response);
        CHECK(response->getHeader("Access-Control-Allow-Headers") == "content-type, x-custom");
    }

    SECTION("empty value falls back to the default")
    {
        Cors::apply(makeRequest(drogon::Options, ""), response);
        CHECK(response->getHeader("Access-Control-Allow-Headers") == Cors::DEFAULT_ALLOWED_HEADERS);
    }

    SECTION("preflight sets Vary")
    {
        Cors::apply(makeRequest(drogon::Options, "content-type"), response);
        CHECK(response->getHeader("Vary") == "Access-Control-Request-Headers");
    }
}

TEST_CASE("Cors::apply echoes the requested headers on a preflight only", "[api][cors]")
{
    SECTION("preflight")
    {
        auto response = drogon::HttpResponse::newHttpResponse();
        Cors::apply(makeRequest(drogon::Options, "content-type, x-custom"), response);

        CHECK(response->getHeader("Access-Control-Allow-Headers") == "content-type, x-custom");
        CHECK(response->getHeader("Access-Control-Max-Age") == Cors::PREFLIGHT_MAX_AGE);
    }
    SECTION("a normal request never echoes request headers")
    {
        auto response = drogon::HttpResponse::newHttpResponse();
        Cors::apply(makeRequest(drogon::Get, "x-evil"), response);

        CHECK(response->getHeader("Access-Control-Allow-Headers") == Cors::DEFAULT_ALLOWED_HEADERS);
    }
}
