#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <optional>
#include <regex>
#include <string>

#include "gatekeeper/keeprbase/keeprbase.hpp"
#include "gatekeeper/types.hpp"
#include "support/builders.hpp"
#include "support/fake_db.hpp"

// Minimal subclass to expose protected methods for testing.
namespace
{
    struct TestKeepr : api::v2::KeeprBase
    {
        using KeeprBase::createTokenVerifier;
        using KeeprBase::current_time_to_utc_string;
        using KeeprBase::decodeToken;
        using KeeprBase::getTokenManagerParameters;
        using KeeprBase::setNowLoginTimeGetLastLogoutTime;
    };

    constexpr const char* kSecret = "valhalla-unit-test-secret-key";
    constexpr const char* kIssuer = "valhalla-unit-test-issuer";
    constexpr const char* kType   = "JWS";
}  // namespace

TEST_CASE("KeeprBase::current_time_to_utc_string returns a UTC timestamp", "[gatekeeper][keeprbase]")
{
    const std::string ts = TestKeepr::current_time_to_utc_string();

    REQUIRE_FALSE(ts.empty());
    // Expected format: "YYYY-MM-DD HH:MM:SS +0000"
    const std::regex utcPattern(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} \+0000)");
    CHECK(std::regex_match(ts, utcPattern));
}

TEST_CASE("KeeprBase::current_time_to_utc_string ends with '+0000'", "[gatekeeper][keeprbase]")
{
    const std::string ts = TestKeepr::current_time_to_utc_string();
    CHECK(ts.size() > 5);
    CHECK(ts.substr(ts.size() - 5) == "+0000");
}

TEST_CASE("KeeprBase::getTokenManagerParameters returns values matching the test environment", "[gatekeeper][keeprbase]")
{
    TestKeepr                                  k;
    const Configurator::TokenManagerParameters params = k.getTokenManagerParameters();

    // The test environment (support/global_env.cpp) sets these env vars.
    CHECK(params.secret == kSecret);
    CHECK(params.issuer == kIssuer);
    CHECK(params.type == kType);
    CHECK(params.validity > 0);
}

TEST_CASE("KeeprBase::createTokenVerifier constructs a verifier that accepts a valid token", "[gatekeeper][keeprbase]")
{
    TestKeepr                                      k;
    const std::string                              llodt = "2026-01-01 00:00:00 +0000";
    std::optional<api::v2::Types::ClientLoginData> data  = valhalla_test::makeLoginData(500, "users", "127.0.0.1");
    data->username                                       = "testuser";
    data->lastLogoutTime                                 = llodt;
    data->token                                          = valhalla_test::makeToken(kSecret, kIssuer, kType, "testuser", 500, "users", "127.0.0.1", llodt);

    // createTokenVerifier must not throw for valid data.
    REQUIRE_NOTHROW([&] { (void)k.createTokenVerifier(data); }());

    // The verifier should accept the token we just created.
    auto verifier     = k.createTokenVerifier(data);
    auto decodedToken = jwt::decode<jwt::traits::kazuho_picojson>(data->token.value());
    CHECK_NOTHROW(verifier.verify(decodedToken));
}

TEST_CASE("KeeprBase::createTokenVerifier throws when login data is incomplete", "[gatekeeper][keeprbase]")
{
    TestKeepr k;

    SECTION("empty optional")
    {
        std::optional<api::v2::Types::ClientLoginData> empty;
        CHECK_THROWS_AS(k.createTokenVerifier(empty), std::runtime_error);
    }

    SECTION("missing username")
    {
        std::optional<api::v2::Types::ClientLoginData> data = valhalla_test::makeLoginData(501, "users");
        data->username.reset();
        CHECK_THROWS_AS(k.createTokenVerifier(data), std::runtime_error);
    }
}

TEST_CASE("KeeprBase::decodeToken extracts claims from a valid JWT", "[gatekeeper][keeprbase]")
{
    const std::string                              llodt = "2026-01-01 00:00:00 +0000";
    std::optional<api::v2::Types::ClientLoginData> data  = valhalla_test::makeLoginData(600, "providers", "10.0.0.1");
    data->username                                       = "drsmith";
    data->lastLogoutTime                                 = llodt;
    data->token                                          = valhalla_test::makeToken(kSecret, kIssuer, kType, "drsmith", 600, "providers", "10.0.0.1", llodt);

    auto decoded = jwt::decode<jwt::traits::kazuho_picojson>(data->token.value());

    std::string message;
    const bool  ok = TestKeepr::decodeToken(data, message, decoded);

    REQUIRE(ok);
    CHECK(message.empty());
    REQUIRE(data->clientId.has_value());
    CHECK(data->clientId.value() == 600);
    CHECK(data->group.value() == "providers");
    CHECK(data->username.value() == "drsmith");
}

TEST_CASE("KeeprBase::decodeToken rejects a mismatched IP address", "[gatekeeper][keeprbase]")
{
    const std::string llodt = "2026-01-01 00:00:00 +0000";
    // Token is issued for IP 192.168.1.1 but data claims 192.168.1.2.
    std::optional<api::v2::Types::ClientLoginData> data = valhalla_test::makeLoginData(601, "users", "192.168.1.2");
    data->username                                      = "user";
    data->lastLogoutTime                                = llodt;
    data->token                                         = valhalla_test::makeToken(kSecret, kIssuer, kType, "user", 601, "users", "192.168.1.1", llodt);

    auto decoded = jwt::decode<jwt::traits::kazuho_picojson>(data->token.value());

    std::string message;
    CHECK_FALSE(TestKeepr::decodeToken(data, message, decoded));
    CHECK(message.find("ip address") != std::string::npos);
}

TEST_CASE("KeeprBase::decodeToken rejects an expired token", "[gatekeeper][keeprbase]")
{
    const std::string                              llodt = "2026-01-01 00:00:00 +0000";
    std::optional<api::v2::Types::ClientLoginData> data  = valhalla_test::makeLoginData(602, "users", "127.0.0.1");
    data->username                                       = "user";
    data->lastLogoutTime                                 = llodt;
    // Token that expired 1 minute ago.
    data->token = valhalla_test::makeToken(kSecret, kIssuer, kType, "user", 602, "users", "127.0.0.1", llodt, std::chrono::minutes{-1});

    auto decoded = jwt::decode<jwt::traits::kazuho_picojson>(data->token.value());

    std::string message;
    CHECK_FALSE(TestKeepr::decodeToken(data, message, decoded));
    CHECK(message.find("expired") != std::string::npos);
}

// Bug 4 fixed: the .value() call on an empty optional in the error branch has
// been removed; the function now returns false cleanly when the DB returns nullopt.
TEST_CASE("KeeprBase::setNowLoginTimeGetLastLogoutTime returns false when DB returns nullopt", "[gatekeeper][keeprbase]")
{
    auto fake          = std::make_shared<FakeDatabaseController>();
    fake->query_result = std::nullopt;
    FakeDbGuard guard(fake);

    TestKeepr k;
    auto      data = std::make_optional<api::v2::Types::ClientLoginData>();
    data->group    = std::string("users");
    data->clientId = uint64_t{42};

    bool result = k.setNowLoginTimeGetLastLogoutTime(data);
    CHECK_FALSE(result);
}
