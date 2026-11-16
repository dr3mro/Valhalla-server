#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <iterator>
#include <ranges>
#include <string>
#include <vector>

#include "server/cors_tokens.hpp"

TEST_CASE("CorsTokens::trim removes tabs and spaces", "[server][cors]")
{
    using CorsTokens::trim;
    CHECK(trim("\t content-type ") == "content-type");
    CHECK(trim("   ") == "");
}

TEST_CASE("CorsTokens::tokens drops empty entries", "[server][cors]")
{
    std::vector<std::string_view> values;
    auto toks = CorsTokens::tokens("a,,b");
    std::copy(toks.begin(), toks.end(), std::back_inserter(values));
    REQUIRE(values.size() == 2);
    CHECK(values[0] == "a");
    CHECK(values[1] == "b");
}

TEST_CASE("CorsTokens::sanitizeRequestedHeaders validates tokens", "[server][cors]")
{
    using CorsTokens::sanitizeRequestedHeaders;
    CHECK(sanitizeRequestedHeaders("\t content-type ,  x-custom  ") == "content-type, x-custom");
    CHECK(sanitizeRequestedHeaders("content-type,,x-custom") == "content-type, x-custom");
    CHECK(sanitizeRequestedHeaders("x-c\xc3\xbustom") == "");
    CHECK(sanitizeRequestedHeaders(std::string(1024, 'a')) != "");
    CHECK(sanitizeRequestedHeaders(std::string(1025, 'a')) == "");
}

TEST_CASE("CorsTokens::mergeVary is case-insensitive", "[server][cors]")
{
    using CorsTokens::mergeVary;
    CHECK(mergeVary("Accept-Encoding, access-control-request-headers", "Access-Control-Request-Headers") == "Accept-Encoding, access-control-request-headers");
    CHECK(mergeVary("*", "Access-Control-Request-Headers") == "*");
    CHECK(mergeVary("", "Access-Control-Request-Headers") == "Access-Control-Request-Headers");
    CHECK(mergeVary("Accept-Encoding", "Access-Control-Request-Headers") == "Accept-Encoding, Access-Control-Request-Headers");
}
