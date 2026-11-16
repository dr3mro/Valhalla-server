/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file test_cors.cpp
 * @brief Checks the exact CORS headers the server adds to HTTP responses,
 * including preflight handling.
 * @details The cases cover a normal response, the low-level header sanitizer,
 * case-insensitive Vary handling, de-duplication and idempotence, and hardening
 * of the attacker-controlled preflight echo. The tests use small
 * request/response helpers so each case can focus on one header rule.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Browsers rely on CORS headers to decide whether a web page may call this API.
 * Missing headers break legitimate browser clients, while a sloppy preflight
 * echo can let a hostile site smuggle arbitrary header values through. These
 * tests pin down the contract so a change cannot break browser access or widen
 * what the server allows.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 test cases and sections:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Catch2 assertions (CHECK, CHECK_FALSE):
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - CORS (browser rules for cross-origin requests):
 * https://developer.mozilla.org/en-US/docs/Web/HTTP/CORS
 * - Lambdas (small inline functions):
 * https://en.cppreference.com/w/cpp/language/lambda
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Request object used to choose normal vs preflight behaviour.
#include <drogon/HttpRequest.h>
// Response object whose headers are inspected.
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>  // HTTP methods such as Get and Options.

// Catch2's TEST_CASE, SECTION and CHECK macros.
#include <catch2/catch_test_macros.hpp>
#include <string>  // std::string for header values and text searches.

#include "server/cors.hpp"  // Cors, the class under test.

// These tests pin down the CORS response contract: which headers are always
// emitted, which are preflight-only, and how the preflight echo is hardened so
// a client cannot inject arbitrary header values.

namespace  // Anonymous namespace keeps the helper private to this file.
{
  /** @brief Builds a request with the method and optional preflight header
   * list that Cors::apply branches on. */
  /**
   * @param method The HTTP method (Get for normal, Options for preflight).
   * @param requestedHeaders Optional value of the
   * Access-Control-Request-Headers header.
   */
  drogon::HttpRequestPtr makeRequest(drogon::HttpMethod method,
                                     const std::string& requestedHeaders = "")
  {
    auto request =
        drogon::HttpRequest::newHttpRequest();  // Start with a fresh, empty
                                                // request.
    request->setMethod(
        method);  // Set the method so Cors can tell preflight from normal.
    if (!requestedHeaders.empty())  // Only add the header when the caller
                                    // supplied a value.
    {
      request->addHeader("Access-Control-Request-Headers",
                         requestedHeaders);  // Attach the header a browser
                                             // sends on preflight.
    }
    return request;  // Hand the prepared request back.
  }
}  // namespace

// A normal response always advertises origins/methods/headers and exposes
// Retry-After; preflight-only headers (Max-Age, and the reflected header list)
// must be absent.
TEST_CASE("Cors::apply sets the full header set on a normal response",
          "[api][cors]")  // The baseline normal-response contract.
{
  auto response =
      drogon::HttpResponse::newHttpResponse();  // A fresh response to fill
                                                // with CORS headers.
  Cors::apply(makeRequest(drogon::Get),
              response);  // Apply CORS using a normal GET request.

  CHECK(response->getHeader("Access-Control-Allow-Origin") ==
        "*");  // Any origin is allowed.
  CHECK(response->getHeader("Access-Control-Allow-Methods").find("PUT") !=
        std::string::npos);  // PUT is advertised.
  CHECK(response->getHeader("Access-Control-Allow-Methods").find("PATCH") !=
        std::string::npos);  // PATCH is advertised.
  CHECK(response->getHeader("Access-Control-Allow-Methods").find("DELETE") !=
        std::string::npos);  // DELETE is advertised.
  CHECK(response->getHeader("Access-Control-Allow-Headers") ==
        Cors::defaultAllowedHeaders());  // The fixed default header list is
                                         // used.
  CHECK(response->getHeader("Access-Control-Expose-Headers")
            .find("Retry-After") !=
        std::string::npos);  // Clients may read Retry-After.
  CHECK(
      response->getHeader("Access-Control-Max-Age").empty());  // preflight only
  CHECK(response->getHeader("Allow").empty());  // not a CORS header
}

// Sanitization must reject non-ASCII bytes and normalize separators so the
// reflected header value stays a safe, well-formed ASCII token list.
TEST_CASE("Cors::detail helpers are ASCII-only and trim tokens",
          "[api][cors]")  // Tests the low-level sanitizer directly.
{
  using Cors::detail::sanitizeRequestedHeaders;  // Shorthand for the function
                                                 // under test.

  // tabs and spaces around tokens are trimmed
  CHECK(sanitizeRequestedHeaders("\t content-type ,  x-custom  ") ==
        "content-type, x-custom");  // Whitespace is trimmed and tokens
                                    // rejoined with ", ".
  // empty tokens between commas are dropped
  CHECK(sanitizeRequestedHeaders("content-type,,x-custom") ==
        "content-type, x-custom");  // Consecutive commas do not create empty
                                    // tokens.
  // a non-ASCII byte makes the whole token invalid
  CHECK(sanitizeRequestedHeaders("x-c\xc3\xbustom") ==
        "");  // A non-ASCII byte invalidates the whole value.
  // a normal ASCII token still validates
  CHECK(sanitizeRequestedHeaders("X-Token_1.2") ==
        "X-Token_1.2");  // Ordinary ASCII names pass through unchanged.
}

// Vary de-duplication compares tokens case-insensitively, as HTTP field values
// are case-insensitive, so an existing lowercase entry is not duplicated.
TEST_CASE(
    "Cors::detail addToVary is case-insensitive",
    "[api][cors]")  // Same header in different case must not be added twice.
{
  auto response = drogon::HttpResponse::newHttpResponse();  // A fresh response.
  response->addHeader("Vary",
                      // Pre-seed the token in lowercase.
                      "Accept-Encoding, access-control-request-headers");

  Cors::addToVary(response,
                  "Access-Control-Request-Headers");  // Try to add the same
                                                      // token in different
                                                      // case.

  CHECK(response->getHeader("Vary") ==
        // Value is unchanged: no duplicate appeared.
        "Accept-Encoding, access-control-request-headers");
}

// An Allow-Origin already set by an upstream layer must not be emitted twice.
TEST_CASE("Cors::apply does not duplicate a pre-existing Allow-Origin",
          "[api][cors]")  // Guards against duplicate headers.
{
  auto response = drogon::HttpResponse::newHttpResponse();  // A fresh response.
  response->addHeader("Access-Control-Allow-Origin",
                      "*");  // Pretend an earlier layer already set it.

  Cors::apply(makeRequest(drogon::Get), response);  // Apply CORS on top.

  CHECK(response->getHeader("Access-Control-Allow-Origin") ==
        "*");  // Value is still "*".
  CHECK(response->headers().count("access-control-allow-origin") ==
        1);  // Exactly one such header exists (case-insensitive lookup).
}

// apply may run more than once per response (e.g. filter and handler), so it
// must be idempotent: a second call leaves the header set unchanged.
TEST_CASE("Cors::apply is idempotent: applying it twice changes nothing",
          "[api][cors]")  // Repeated application is safe.
{
  // The response being modified.
  auto       response = drogon::HttpResponse::newHttpResponse();
  const auto request =
      makeRequest(drogon::Get);  // One request reused for both calls.

  Cors::apply(request, response);  // First application.
  const auto once = response->getHeader(
      "Access-Control-Allow-Origin");  // Remember the value after one call.
  const auto size =
      response->headers().size();  // Remember how many headers existed.

  Cors::apply(request, response);  // Second application.
  CHECK(response->getHeader("Access-Control-Allow-Origin") ==
        once);                                // Value is unchanged.
  CHECK(response->headers().size() == size);  // No extra headers were added.
}

// Vary must not be clobbered: a wildcard stays a wildcard, existing tokens in
// any case are preserved, and non-preflight responses do not introduce Vary.
TEST_CASE("Cors::apply Vary edge cases",
          "[api][cors]")  // Several independent Vary rules.
{
  auto response =
      drogon::HttpResponse::newHttpResponse();  // Shared response; each
                                                // SECTION starts from a fresh
                                                // test run.

  SECTION("Vary: * stays unchanged")  // Wildcard must survive untouched.
  {
    response->addHeader("Vary", "*");  // Set the wildcard.
    Cors::apply(makeRequest(drogon::Options, "content-type"),
                response);                      // Apply a preflight request.
    CHECK(response->getHeader("Vary") == "*");  // Wildcard is preserved.
  }

  SECTION(
      // Case-insensitive de-duplication.
      "a Vary token in a different case is not duplicated")
  {
    response->addHeader("Vary",
                        // Seed tokens in mixed case.
                        "accept-encoding, ACCESS-CONTROL-REQUEST-HEADERS");
    Cors::apply(makeRequest(drogon::Options, "content-type"),
                response);  // Apply preflight.
    CHECK(response->getHeader("Vary") ==
          // No duplicate token added.
          "accept-encoding, ACCESS-CONTROL-REQUEST-HEADERS");
  }

  SECTION(
      // Only preflight adds these headers.
      "a non-preflight response gets no Max-Age or Vary")
  {
    Cors::apply(makeRequest(drogon::Get), response);  // Normal request.
    CHECK(response->getHeader("Access-Control-Max-Age")
              .empty());                         // No cache hint.
    CHECK(response->getHeader("Vary").empty());  // No Vary header.
  }

  SECTION("apply twice yields identical headers")  // Idempotence across the
                                                   // whole header set.
  {
    Cors::apply(makeRequest(drogon::Get), response);  // First call.
    const auto first = response->headers();           // Snapshot all headers.
    Cors::apply(makeRequest(drogon::Get), response);  // Second call.
    CHECK(response->headers() == first);  // Entire header map is unchanged.
  }
}

// Existing Vary values from other layers must survive; our token is appended
// exactly once even when apply runs repeatedly.
TEST_CASE("Cors::apply preserves and appends Vary",
          "[api][cors]")  // Rules for appending our Vary token.
{
  auto response = drogon::HttpResponse::newHttpResponse();  // Shared response
                                                            // for the sections.

  SECTION("no existing Vary gets exactly ours")  // Nothing to preserve.
  {
    Cors::apply(makeRequest(drogon::Options, "content-type"),
                response);  // Preflight with no prior Vary.
    CHECK(response->getHeader("Vary") ==
          "Access-Control-Request-Headers");  // Only our token is present.
  }

  SECTION(
      // Combine with an existing token.
      "an existing Vary is preserved and ours is appended")
  {
    response->addHeader("Vary",
                        "Accept-Encoding");  // Pre-existing token from another
                                             // layer.
    Cors::apply(makeRequest(drogon::Options, "content-type"),
                response);  // Preflight.
    CHECK(response->getHeader("Vary") ==
          // Both tokens, ours appended.
          "Accept-Encoding, Access-Control-Request-Headers");
  }

  SECTION("applying twice does not duplicate the token")  // Repeated appends
                                                          // are collapsed.
  {
    Cors::apply(makeRequest(drogon::Options, "content-type"),
                response);  // First preflight.
    Cors::apply(makeRequest(drogon::Options, "content-type"),
                response);  // Second preflight.
    CHECK(response->getHeader("Vary") ==
          "Access-Control-Request-Headers");  // Token appears exactly once.
  }
}

// The requested-header list is attacker-controlled input. Oversized values,
// embedded spaces/control characters and mixed invalid tokens must never widen
// the response beyond the default allow-list.
TEST_CASE("Cors::apply hardens the preflight echo",
          "[api][cors]")  // Security-focused cases for hostile input.
{
  auto response = drogon::HttpResponse::newHttpResponse();  // Shared response
                                                            // for the sections.

  SECTION("oversized value falls back to the default")  // Too-long input is
                                                        // rejected wholesale.
  {
    const std::string
        huge(1025, 'a');  // Build a value just over the 1024-byte limit.
    Cors::apply(makeRequest(drogon::Options, huge),
                response);  // Preflight with the oversized value.
    CHECK(response->getHeader("Access-Control-Allow-Headers") ==
          Cors::defaultAllowedHeaders());  // Falls back to the safe default.
  }

  SECTION(
      // Embedded whitespace/control bytes are unsafe.
      "a value with a space or control character is rejected")
  {
    Cors::apply(makeRequest(drogon::Options, "x bad"),
                response);  // A space inside a token.
    CHECK(response->getHeader("Access-Control-Allow-Headers") ==
          Cors::defaultAllowedHeaders());  // Rejected.
    const std::string withControl =
        std::string("\x01authorization");  // A control character at the start.
    Cors::apply(makeRequest(drogon::Options, withControl),
                response);  // Preflight with the control byte.
    CHECK(response->getHeader("Access-Control-Allow-Headers") ==
          Cors::defaultAllowedHeaders());  // Rejected too.
  }

  SECTION(
      // Partial acceptance.
      "mixed valid and invalid tokens keeps only the valid ones")
  {
    Cors::apply(makeRequest(drogon::Options,
                            "content-type, not a token, x-custom"),
                response);  // One invalid token among valid ones.
    CHECK(response->getHeader("Access-Control-Allow-Headers") ==
          "content-type, x-custom");  // Only the valid tokens survive.
  }

  SECTION("empty value falls back to the default")  // No requested headers
                                                    // means use the default.
  {
    Cors::apply(makeRequest(drogon::Options, ""),
                response);  // Preflight with an empty header value.
    CHECK(response->getHeader("Access-Control-Allow-Headers") ==
          Cors::defaultAllowedHeaders());  // Safe default is used.
  }

  SECTION("preflight sets Vary")  // Confirm preflight's Vary side effect.
  {
    Cors::apply(makeRequest(drogon::Options, "content-type"),
                response);  // Preflight.
    CHECK(response->getHeader("Vary") ==
          "Access-Control-Request-Headers");  // Vary is set for caching
                                              // correctness.
  }
}

// Only an OPTIONS preflight reflects and caches the requested headers; a
// normal GET must always use the fixed default list.
TEST_CASE("Cors::apply echoes the requested headers on a preflight only",
          "[api][cors]")  // Contrasts preflight and normal.
{
  SECTION("preflight")  // OPTIONS request.
  {
    auto response = drogon::HttpResponse::newHttpResponse();  // Fresh response.
    Cors::apply(makeRequest(drogon::Options, "content-type, x-custom"),
                response);  // Preflight echoing two headers.

    CHECK(response->getHeader("Access-Control-Allow-Headers") ==
          "content-type, x-custom");  // The requested headers are reflected.
    CHECK(response->getHeader("Access-Control-Max-Age") ==
          Cors::preflightMaxAge());  // A cache lifetime is added for
                                     // preflight.
  }
  SECTION("a normal request never echoes request headers")  // GET request with
                                                            // a header value.
  {
    auto response = drogon::HttpResponse::newHttpResponse();  // Fresh response.
    Cors::apply(makeRequest(drogon::Get, "x-evil"),
                response);  // Normal request trying to inject a header.

    CHECK(response->getHeader("Access-Control-Allow-Headers") ==
          Cors::defaultAllowedHeaders());  // The default list wins; no
                                           // reflection.
  }
}
