#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>

#include "api/v2/filters/auth.hpp"
#include "utils/jsonhelper/jsonhelper.hpp"

using AuthFilter = api::v2::Filters::Auth;

namespace
{
    /// Runs the filter on a prepared request. Returns the response the filter
    /// passed to its rejection callback; sets chainCalled if the filter
    /// instead passed control to the chain.
    drogon::HttpResponsePtr runFilter(const std::function<void(const drogon::HttpRequestPtr &)> &prepare, bool &chainCalled)
    {
        auto request = drogon::HttpRequest::newHttpRequest();
        if (prepare)
        {
            prepare(request);
        }

        drogon::HttpResponsePtr rejected;
        chainCalled = false;

        AuthFilter filter;
        filter.doFilter(request,  //
            [&rejected](const drogon::HttpResponsePtr &response) { rejected = response; }, [&chainCalled] { chainCalled = true; });

        return rejected;
    }
}  // namespace

// NOTE: Only the 401 rejection paths are exercised. The success path needs a
// real client address on the request (setPeerAddr lives on HttpRequestImpl,
// not the interface) and a verifiable token, which requires the database -
// out of scope for hermetic unit tests (tests/README.md).

TEST_CASE("Auth filter rejects a request without an Authorization header", "[api][auth]")
{
    bool chainCalled = false;
    const auto response = runFilter(nullptr, chainCalled);

    REQUIRE(response != nullptr);
    CHECK_FALSE(chainCalled);
    CHECK(response->getStatusCode() == drogon::HttpStatusCode::k401Unauthorized);
    CHECK(response->getBody().find("failed to extract token") != std::string::npos);
}

TEST_CASE("Auth filter rejects a non-bearer Authorization header", "[api][auth]")
{
    bool chainCalled = false;
    const auto response = runFilter(
        [](const drogon::HttpRequestPtr &request) { request->addHeader("Authorization", "Basic dXNlcjpwYXNz"); }, chainCalled);

    REQUIRE(response != nullptr);
    CHECK_FALSE(chainCalled);
    CHECK(response->getStatusCode() == drogon::HttpStatusCode::k401Unauthorized);
    CHECK(response->getBody().find("failed to extract token") != std::string::npos);
}

TEST_CASE("Auth filter rejects a bare 'Bearer' prefix without the token", "[api][auth]")
{
    // "Bearer" alone is size <= bearerMagic (7) and cannot carry a token.
    bool chainCalled = false;
    const auto response = runFilter(
        [](const drogon::HttpRequestPtr &request) { request->addHeader("Authorization", "Bearer"); }, chainCalled);

    REQUIRE(response != nullptr);
    CHECK_FALSE(chainCalled);
    CHECK(response->getStatusCode() == drogon::HttpStatusCode::k401Unauthorized);
}

TEST_CASE("Auth filter rejects a Bearer token that does not verify", "[api][auth]")
{
    bool chainCalled = false;
    const auto response = runFilter(
        [](const drogon::HttpRequestPtr &request) { request->addHeader("Authorization", "Bearer not-a-real-token"); }, chainCalled);

    REQUIRE(response != nullptr);
    CHECK_FALSE(chainCalled);
    CHECK(response->getStatusCode() == drogon::HttpStatusCode::k401Unauthorized);
    CHECK_FALSE(response->getBody().empty());
}
