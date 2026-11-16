/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file test_cors_tokens.cpp
 * @brief Checks the low-level string helpers used to build safe CORS header
 * values.
 * @details The cases cover trimming whitespace, splitting a comma-separated
 * list into tokens, validating tokens (ASCII-only and within a size limit), and
 * merging a token into an existing Vary header. Together they pin down the
 * exact rules that the higher-level CORS code relies on.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * These helpers process header values supplied by browsers, which means hostile
 * input. If token parsing accepted empty entries, non-ASCII bytes or oversized
 * values, the server could echo unsafe data back into a response. These tests
 * keep that parsing strict and predictable.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 test cases and assertions:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Ranges and views (lazy views over a sequence):
 * https://en.cppreference.com/w/cpp/ranges
 * - std::string_view (a non-owning view of text):
 * https://en.cppreference.com/w/cpp/string/basic_string_view
 * - CORS (browser rules for cross-origin requests):
 * https://developer.mozilla.org/en-US/docs/Web/HTTP/CORS
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// std::ranges::copy, used to collect tokens into a vector.
#include <algorithm>
#include <catch2/catch_test_macros.hpp>  // Catch2's TEST_CASE and CHECK macros.
// std::back_inserter, used as the output target of the copy.
#include <iterator>
#include <ranges>  // Range support for the token view.
#include <string>  // std::string, used for the test inputs.
#include <vector>  // std::vector, used to hold the collected tokens.

#include "server/cors_tokens.hpp"  // CorsTokens, the set of helpers under test.

// Low-level string helpers used by CORS processing. These tests pin the exact
// tokenization rules the higher-level Cors::apply behaviour relies on.

// Trimming tabs/spaces is what lets header values with incidental whitespace
// compare and validate correctly.
TEST_CASE("CorsTokens::trim removes tabs and spaces",
    "[server][cors]")  // Whitespace-only and padded inputs.
{
  using CorsTokens::trim;  // Shorthand for the function under test.
  CHECK(trim("\t content-type ") ==
        "content-type");     // Leading tab and trailing space are removed.
  CHECK(trim("   ") == "");  // A whitespace-only string becomes empty.
}

// Consecutive commas must not produce empty tokens in the reflected list.
TEST_CASE("CorsTokens::tokens drops empty entries",
    "[server][cors]")  // Splitting behaviour.
{
  std::vector<std::string_view>
       values;  // Holds the tokens copied out of the lazy view.
  auto toks = CorsTokens::tokens("a,,b");  // Get a view that splits on commas.
  std::ranges::copy(toks.begin(), toks.end(),
      std::back_inserter(
          values));  // Copy the view into the vector so we can index it.
  REQUIRE(values.size() ==
          2);  // The empty slot from ",," was dropped, leaving two tokens.
  CHECK(values[0] == "a");  // First token is preserved.
  CHECK(values[1] == "b");  // Second token is preserved.
}

// Reject non-ASCII and over-limit (1025-byte) header lists while allowing the
// boundary case at 1024, to keep the preflight echo bounded and safe.
TEST_CASE("CorsTokens::sanitizeRequestedHeaders validates tokens",
    "[server][cors]")  // Validation limits.
{
  using CorsTokens::sanitizeRequestedHeaders;  // Shorthand for the function
                                               // under test.
  CHECK(sanitizeRequestedHeaders("\t content-type ,  x-custom  ") ==
        "content-type, x-custom");  // Whitespace is trimmed and tokens
                                    // normalized.
  CHECK(sanitizeRequestedHeaders("content-type,,x-custom") ==
        "content-type, x-custom");  // Empty comma entries are dropped.
  CHECK(sanitizeRequestedHeaders("x-c\xc3\xbustom") ==
        "");  // Non-ASCII bytes make the value invalid.
  CHECK(sanitizeRequestedHeaders(std::string(1024, 'a')) !=
        "");  // Exactly 1024 bytes is still accepted.
  CHECK(sanitizeRequestedHeaders(std::string(1025, 'a')) ==
        "");  // 1025 bytes is rejected as too large.
}

// Merging a token into an existing Vary must be case-insensitive, preserve a
// wildcard, tolerate empty/odd input, and append exactly once.
TEST_CASE("CorsTokens::mergeVary is case-insensitive",
    "[server][cors]")  // Vary merge rules.
{
  using CorsTokens::mergeVary;  // Shorthand for the function under test.
  CHECK(mergeVary("Accept-Encoding, access-control-request-headers",
            "Access-Control-Request-Headers") ==
        // Token already there (different case) is not duplicated.
        "Accept-Encoding, access-control-request-headers");
  CHECK(mergeVary("*", "Access-Control-Request-Headers") ==
        "*");  // A wildcard stays a wildcard.
  CHECK(mergeVary("", "Access-Control-Request-Headers") ==
        "Access-Control-Request-Headers");  // Empty input yields just the new
                                            // token.
  CHECK(mergeVary("Accept-Encoding", "Access-Control-Request-Headers") ==
        // New token is appended to the existing one.
        "Accept-Encoding, Access-Control-Request-Headers");
  CHECK(mergeVary("  Accept-Encoding , , ", "Access-Control-Request-Headers") ==
        // Odd spacing and empty entries are cleaned up.
        "Accept-Encoding, Access-Control-Request-Headers");
  CHECK(mergeVary("", "Access-Control-Request-Headers") ==
        "Access-Control-Request-Headers");  // Repeating the empty case
                                            // confirms consistency.
}
