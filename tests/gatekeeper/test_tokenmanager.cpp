#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <optional>
#include <string>

#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/kazuho-picojson/traits.h>

#include "gatekeeper/tokenmanager/tokenmanager.hpp"
#include "support/builders.hpp"

using TokenManager     = api::v2::TokenManager;
using ClientLoginData  = api::v2::Types::ClientLoginData;
using valhalla_test::makeLoginData;

namespace
{
    // Test token parameters - must match tests/support/global_env.cpp, which
    // sets them before the Configurator singleton is created.
    constexpr const char *kSecret = "valhalla-unit-test-secret-key";
    constexpr const char *kIssuer = "valhalla-unit-test-issuer";
    constexpr const char *kType   = "JWS";
    constexpr int         kValidityMinutes = 60;
}  // namespace

TEST_CASE("TokenManager issues a token for valid login data", "[gatekeeper][token]")
{
    TokenManager manager;

    std::optional<ClientLoginData> loginData = makeLoginData(42, "users", "10.1.2.3");
    loginData->username                      = "doctor.who";
    loginData->lastLogoutTime                = "2026-01-01 00:00:00 +0000";
    loginData->group                         = "users";

    REQUIRE(manager.generateToken(loginData));
    REQUIRE(loginData->token.has_value());
    REQUIRE_FALSE(loginData->token->empty());
}

TEST_CASE("TokenManager token contains the expected claims", "[gatekeeper][token]")
{
    TokenManager manager;

    std::optional<ClientLoginData> loginData = makeLoginData(42, "users", "10.1.2.3");
    loginData->username                      = "doctor.who";
    loginData->lastLogoutTime                = "2026-01-01 00:00:00 +0000";
    loginData->group                         = "users";

    REQUIRE(manager.generateToken(loginData));
    REQUIRE(loginData->token.has_value());

    const auto decoded = jwt::decode<jwt::traits::kazuho_picojson>(loginData->token.value());

    CHECK(decoded.get_issuer() == kIssuer);
    CHECK(decoded.get_type() == kType);
    CHECK(decoded.get_subject() == "doctor.who");
    CHECK(decoded.get_id() == "42");
    CHECK(decoded.get_payload_claim("ip_address").as_string() == "10.1.2.3");
    CHECK(decoded.get_payload_claim("llodt").as_string() == "2026-01-01 00:00:00 +0000");
    CHECK(decoded.get_payload_claim("group").as_string() == "users");
    CHECK(decoded.has_payload_claim("exp"));
    CHECK(decoded.has_payload_claim("iat"));

    // Expiry is ~TOKEN_VALIDITY minutes after issuance.
    const auto expiry = decoded.get_expires_at();
    const auto issued = decoded.get_issued_at();
    const auto delta  = std::chrono::duration_cast<std::chrono::minutes>(expiry - issued);
    CHECK(delta.count() == kValidityMinutes);
}

TEST_CASE("TokenManager token signature round-trips with the configured secret", "[gatekeeper][token]")
{
    TokenManager manager;

    std::optional<ClientLoginData> loginData = makeLoginData();
    loginData->username                      = "roundtrip";
    loginData->lastLogoutTime                = "llodt";
    loginData->group                         = "users";

    REQUIRE(manager.generateToken(loginData));

    // Verify with the same parameters the manager signed with.
    auto verifier = jwt::verify<jwt::traits::kazuho_picojson>()
                        .allow_algorithm(jwt::algorithm::hs256{kSecret})
                        .with_issuer(kIssuer)
                        .with_type(kType);
    auto decoded = jwt::decode<jwt::traits::kazuho_picojson>(loginData->token.value());
    REQUIRE_NOTHROW(verifier.verify(decoded));

    // A verifier with a different secret must reject it.
    auto wrongVerifier = jwt::verify<jwt::traits::kazuho_picojson>().allow_algorithm(jwt::algorithm::hs256{"wrong-secret"});
    REQUIRE_THROWS_AS(wrongVerifier.verify(decoded), std::exception);
}

TEST_CASE("TokenManager fails cleanly when login data is incomplete", "[gatekeeper][token]")
{
    TokenManager manager;

    std::optional<ClientLoginData> loginData = makeLoginData();
    loginData->username.reset();  // generateToken dereferences username

    REQUIRE_FALSE(manager.generateToken(loginData));
    // No token was set.
    REQUIRE_FALSE(loginData->token.has_value());
}

TEST_CASE("TokenManager validates a well-formed token whose session fields match", "[gatekeeper][token]")
{
    TokenManager manager;

    // isTokenValid() consults the database for the last logout time; with no
    // database the caller-provided fields decide the outcome (documented in
    // tests/README.md). The token check itself (signature, issuer, type,
    // subject, id, ip, group) is exercised here.
    const auto token    = valhalla_test::makeToken(kSecret, kIssuer, kType, "alice", 7, "users", "127.0.0.1", "2026-01-01 00:00:00 +0000");
    auto loginData = std::optional<ClientLoginData>(makeLoginData(7, "users", "127.0.0.1"));
    loginData->token         = token;
    loginData->username      = "alice";
    loginData->lastLogoutTime = "2026-01-01 00:00:00 +0000";

    std::string message;
    CHECK(manager.isTokenValid(loginData, message));
    CHECK(message.empty());
}

TEST_CASE("TokenManager rejects a token signed with the wrong secret", "[gatekeeper][token]")
{
    TokenManager manager;

    auto loginData = std::optional<ClientLoginData>(makeLoginData(7, "users", "127.0.0.1"));
    loginData->token          = valhalla_test::makeToken("intruder-secret", kIssuer, kType, "alice", 7, "users", "127.0.0.1", "llodt");
    loginData->username       = "alice";
    loginData->lastLogoutTime = "llodt";

    std::string message;
    REQUIRE_FALSE(manager.isTokenValid(loginData, message));
    CHECK_FALSE(message.empty());
}

TEST_CASE("TokenManager rejects a tampered token", "[gatekeeper][token]")
{
    TokenManager manager;

    auto loginData          = std::optional<ClientLoginData>(makeLoginData(7, "users", "127.0.0.1"));
    const auto goodToken    = valhalla_test::makeToken(kSecret, kIssuer, kType, "alice", 7, "users", "127.0.0.1", "llodt");
    loginData->token        = valhalla_test::makeTamperedToken(goodToken);
    loginData->username     = "alice";
    loginData->lastLogoutTime = "llodt";

    std::string message;
    REQUIRE_FALSE(manager.isTokenValid(loginData, message));
    CHECK_FALSE(message.empty());
}

TEST_CASE("TokenManager rejects a malformed token without crashing", "[gatekeeper][token]")
{
    TokenManager manager;

    const auto &malformed = GENERATE(std::string(""), std::string("not-a-token"), std::string("a.b"), std::string("..."),
        std::string("eyJhbGciOiJIUzI1NiJ9.e30.!!!invalid-base64!!!"));

    auto loginData      = std::optional<ClientLoginData>(makeLoginData());
    loginData->token    = malformed;
    std::string message;

    CAPTURE(malformed);
    REQUIRE_FALSE(manager.isTokenValid(loginData, message));
    CHECK_FALSE(message.empty());
}
TEST_CASE("TokenManager generateToken fails cleanly for each missing field", "[gatekeeper][token]")
{
    TokenManager manager;

    SECTION("missing clientId")
    {
        auto loginData        = std::optional<ClientLoginData>(makeLoginData(42, "users", "1.2.3.4"));
        loginData->username   = "u";
        loginData->lastLogoutTime = "llodt";
        loginData->group      = "users";
        loginData->clientId.reset();

        REQUIRE_FALSE(manager.generateToken(loginData));
        CHECK_FALSE(loginData->token.has_value());
    }

    SECTION("missing ip_address")
    {
        auto loginData            = std::optional<ClientLoginData>(makeLoginData(42, "users", "1.2.3.4"));
        loginData->username       = "u";
        loginData->lastLogoutTime = "llodt";
        loginData->group          = "users";
        loginData->ip_address.reset();

        REQUIRE_FALSE(manager.generateToken(loginData));
    }

    SECTION("missing group")
    {
        auto loginData            = std::optional<ClientLoginData>(makeLoginData(42, "users", "1.2.3.4"));
        loginData->username       = "u";
        loginData->lastLogoutTime = "llodt";
        loginData->group.reset();

        REQUIRE_FALSE(manager.generateToken(loginData));
    }

    SECTION("missing lastLogoutTime")
    {
        auto loginData        = std::optional<ClientLoginData>(makeLoginData(42, "users", "1.2.3.4"));
        loginData->username   = "u";
        loginData->group      = "users";
        loginData->lastLogoutTime.reset();

        REQUIRE_FALSE(manager.generateToken(loginData));
    }
}

TEST_CASE("TokenManager rejects an already-expired token", "[gatekeeper][token]")
{
    TokenManager manager;

    // Build a token that expired 1 minute ago.
    const auto token = valhalla_test::makeToken(
        kSecret, kIssuer, kType, "alice", 7, "users", "127.0.0.1", "llodt",
        std::chrono::minutes{-1});

    auto loginData            = std::optional<ClientLoginData>(makeLoginData(7, "users", "127.0.0.1"));
    loginData->token          = token;
    loginData->username       = "alice";
    loginData->lastLogoutTime = "llodt";

    std::string message;
    REQUIRE_FALSE(manager.isTokenValid(loginData, message));
    CHECK_FALSE(message.empty());
}
