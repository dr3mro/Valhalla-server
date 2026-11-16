/**
 * @file test_filters.cpp
 * @brief Checks that the authentication filter rejects requests that have no
 * usable Bearer token.
 * @details Each case prepares a request in a different way — no header, a
 * non-Bearer scheme, a bare "Bearer" word, or an invalid token — and asserts
 * that the filter answers 401 without ever calling the next handler. A small
 * helper runs the filter and reports both the rejection response and whether
 * the chain was allowed to continue.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * The auth filter is the front door for every protected route. If it ever let a
 * bad request through, unauthenticated users could reach handlers that should
 * require a token. These tests pin down the rejection behaviour so a change to
 * token parsing cannot silently open that door.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 test cases and assertions (CHECK_FALSE, REQUIRE):
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - Lambdas (small inline functions used as callbacks):
 * https://en.cppreference.com/w/cpp/language/lambda
 * - std::function (a type-erased callable):
 * https://en.cppreference.com/w/cpp/utility/functional/function
 * - Catch2 test cases and sections:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#include <drogon/HttpRequest.h>  // The request object the filter inspects.
// The response object the filter produces on rejection.
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>  // HTTP status codes such as k401Unauthorized.

// Catch2's TEST_CASE, CHECK and CHECK_FALSE macros.
#include <catch2/catch_test_macros.hpp>
// std::function, used for the callback-preparation parameter.
#include <functional>
#include <memory>   // Smart pointers for Drogon's request/response objects.
#include <string>   // std::string for header values and body checks.
#include <utility>  // Utility support used by callback helpers.

#include "api/v2/filters/auth.hpp"  // Auth, the filter under test.
// JSON helpers used elsewhere with the auth filter.
#include "utils/jsonhelper/jsonhelper.hpp"

using AuthFilter = api::v2::Filters::Auth;  // Shorter name for the filter type.

namespace  // Anonymous namespace keeps the helper private to this file.
{
  /**
   * Runs the filter on a prepared request. Returns the response the filter
   * passed to its rejection callback; sets chainCalled if the filter
   * instead passed control to the chain.
   * @brief Drives one request through the filter and captures what it did.
   * @param prepare Optional function that adds headers to the request before
   * filtering.
   * @param chainCalled Set to true if the filter allowed the request to
   * continue.
   * @return The rejection response, or null if the filter did not reject.
   */
  drogon::HttpResponsePtr runFilter(
      const std::function<void(const drogon::HttpRequestPtr&)>& prepare,
      bool&                                                     chainCalled)
  {
    auto request =
        drogon::HttpRequest::newHttpRequest();  // Start with a clean, empty
                                                // request.
    if (prepare)  // Only run the preparation function if one was supplied.
    {
      prepare(request);  // Let the caller add the header this case wants
                         // to test.
    }

    drogon::HttpResponsePtr
        rejected;  // Will hold the rejection response, if one is produced.
    chainCalled = false;  // Assume the chain was not called until the
                          // callback proves otherwise.

    AuthFilter filter;  // The filter instance under test.
    filter.doFilter(    // Run the filter with two callbacks.
        request,        // The prepared request.
        [&rejected](const drogon::HttpResponsePtr& response)
        {
          rejected = response;
        },
        [&chainCalled]
        {
          chainCalled = true;
        });  // Rejection callback stores the response; chain callback
             // records that the request was allowed through.

    return rejected;  // Give the test the response to assert on.
  }
}  // namespace

// NOTE: Only the 401 rejection paths are exercised. The success path needs a
// real client address on the request (setPeerAddr lives on HttpRequestImpl,
// not the interface) and a verifiable token, which requires the database -
// out of scope for hermetic unit tests (tests/README.md).

// A missing Authorization header must be rejected before the chain runs, so
// unauthenticated requests cannot reach protected handlers.
TEST_CASE("Auth filter rejects a request without an Authorization header",
          "[api][auth]")  // No header at all.
{
  bool chainCalled =
      false;  // Tracks whether the filter let the request through.
  const auto response = runFilter(nullptr,
                                  chainCalled);  // Run with no preparation, so
                                                 // the header is missing.

  REQUIRE(response !=
          nullptr);  // A rejection response must exist before we inspect it.
  CHECK_FALSE(chainCalled);  // Prove the chain was NOT called: the request
                             // stopped here.
  CHECK(response->getStatusCode() ==
        drogon::HttpStatusCode::k401Unauthorized);  // Rejection must be HTTP
                                                    // 401.
  CHECK(response->getBody().find("failed to extract token") !=
        std::string::npos);  // The body explains why it was rejected.
}

// Only Bearer credentials are accepted; other schemes are rejected outright so
// they can never be mistaken for a valid token.
TEST_CASE("Auth filter rejects a non-bearer Authorization header",
          "[api][auth]")  // Uses Basic instead of Bearer.
{
  bool chainCalled =
      false;  // Tracks whether the filter let the request through.
  const auto response = runFilter(
      [](const drogon::HttpRequestPtr& request)
      {
        request->addHeader("Authorization", "Basic dXNlcjpwYXNz");
      },
      chainCalled);  // Add a Basic-auth header.

  REQUIRE(response != nullptr);  // A rejection response must exist.
  CHECK_FALSE(
      chainCalled);  // The chain must not run for the wrong auth scheme.
  CHECK(response->getStatusCode() ==
        drogon::HttpStatusCode::k401Unauthorized);  // Still a 401.
  CHECK(response->getBody().find("failed to extract token") !=
        std::string::npos);  // Same "could not read token" message.
}

TEST_CASE("Auth filter rejects a bare 'Bearer' prefix without the token",
          "[api][auth]")  // Header has the scheme but no token.
{
  // "Bearer" alone is size <= bearerMagic (7) and cannot carry a token.
  bool chainCalled =
      false;  // Tracks whether the filter let the request through.
  const auto response = runFilter(
      [](const drogon::HttpRequestPtr& request)
      {
        request->addHeader("Authorization", "Bearer");
      },
      chainCalled);  // Add only the word "Bearer".

  REQUIRE(response != nullptr);  // A rejection response must exist.
  CHECK_FALSE(chainCalled);      // The chain must not run.
  CHECK(response->getStatusCode() ==
        drogon::HttpStatusCode::k401Unauthorized);  // A bare prefix is still
                                                    // rejected with 401.
}

// A syntactically Bearer-prefixed but invalid token must still be rejected;
// presence of the scheme alone is not proof of authentication.
TEST_CASE("Auth filter rejects a Bearer token that does not verify",
          "[api][auth]")  // Uses a fake, unverifiable token.
{
  bool chainCalled =
      false;  // Tracks whether the filter let the request through.
  const auto response = runFilter(
      [](const drogon::HttpRequestPtr& request)
      {
        request->addHeader("Authorization", "Bearer not-a-real-token");
      },
      chainCalled);  // Add a Bearer token that cannot be validated.

  REQUIRE(response != nullptr);  // A rejection response must exist.
  CHECK_FALSE(chainCalled);      // The chain must not run.
  CHECK(response->getStatusCode() ==
        drogon::HttpStatusCode::k401Unauthorized);  // Invalid token gets 401.
  CHECK_FALSE(response->getBody().empty());         // The response explains the
                                             // failure, so it is not empty.
}
