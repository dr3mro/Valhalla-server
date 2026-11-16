/**
 * @file test_tokenmanager.cpp
 * @brief Catch2 unit tests and fixtures for tokenmanager. Checks JWT issuance,
 * claim contents, signature verification and token validation.
 * @details TokenManager generates signed JSON Web Tokens from login data and
 * validates them later. Using tokens built with the test secret and issuer, the
 * tests check that valid login data yields a non-empty token carrying every
 * expected claim over the right validity window, that the signature round-trips
 * with the configured secret but fails with a different one, that missing
 * required fields fail cleanly, and that isTokenValid rejects wrong-secret,
 * tampered, malformed and expired tokens.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It protects tokenmanager.hpp, the component that issues and checks the bearer
 * tokens used on every authenticated request. If claims were omitted or
 * mis-signed, clients could not log in or tokens from elsewhere could be
 * accepted; if expiry were not enforced, a stolen token would work forever.
 * These tests keep that security-critical contract intact.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE / SECTION:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Catch2 CHECK / REQUIRE / REQUIRE_THROWS_AS assertion macros:
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - Catch2 data generators (GENERATE) to try several bad inputs:
 * https://github.com/catchorg/Catch2/blob/devel/docs/generators.md
 * - JSON Web Tokens: header, payload, claims and signature:
 * https://jwt.io/introduction
 * - std::chrono durations and time points (used for the validity window):
 * https://en.cppreference.com/w/cpp/chrono
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// The jwt-cpp library: decoding, claims and verification used in the
// assertions.
#include <jwt-cpp/jwt.h>
// Selects picojson as the JSON backend for jwt-cpp.
#include <jwt-cpp/traits/kazuho-picojson/traits.h>

// Provides TEST_CASE, SECTION and the CHECK/REQUIRE family of macros.
#include <catch2/catch_test_macros.hpp>
// GENERATE, used to try several malformed token strings.
#include <catch2/generators/catch_generators.hpp>
// Standard minutes and duration_cast for the expiry assertion.
#include <chrono>
#include <optional>  // std::optional for login data and token values.
#include <string>    // std::string for tokens, secrets and messages.

// The production header under test: TokenManager.
#include "gatekeeper/tokenmanager/tokenmanager.hpp"
// makeLoginData / makeToken / makeTamperedToken test helpers.
#include "support/builders.hpp"

using TokenManager =
    api::v2::TokenManager;  // Short alias for the class under test.
using ClientLoginData =
    api::v2::Types::ClientLoginData;  // Short alias for the login-data type.
using valhalla_test::makeLoginData;   // Import the builder so tests can use it
                                      // unqualified.

namespace
{
  // Test token parameters - must match tests/support/global_env.cpp, which
  // sets them before the Configurator singleton is created.
  constexpr const char* kSecret =
      "valhalla-unit-test-secret-key";  // Signing secret, matching the
                                        // configured value.
  constexpr const char* kIssuer =
      "valhalla-unit-test-issuer";      // Expected issuer claim.
  constexpr const char* kType = "JWS";  // Expected token type claim.
  constexpr int         kValidityMinutes =
      60;  // Expected gap between issue time and expiry.
}  // namespace

TEST_CASE("TokenManager issues a token for valid login data",
          "[gatekeeper][token]")  // Registers the basic token-generation test.
{
  TokenManager manager;  // The class under test.

  std::optional<ClientLoginData> loginData =
      makeLoginData(42,
                    "users",
                    "10.1.2.3");  // Build complete login data for client 42.
  loginData->username = "doctor.who";  // Subject to embed in the token.
  loginData->lastLogoutTime =
      "2026-01-01 00:00:00 +0000";  // Last-logout claim to embed.
  loginData->group = "users";       // Group claim to embed.

  REQUIRE(manager.generateToken(
      loginData));  // Prove token generation reports success.
  REQUIRE(loginData->token.has_value());  // Prove a token was actually
                                          // written into the login data.
  REQUIRE_FALSE(loginData->token
                    ->empty());  // Prove the token is a real non-empty string.
}

TEST_CASE("TokenManager token contains the expected claims",
          "[gatekeeper][token]")  // Registers the test that every required
                                  // claim is present and correct.
{
  TokenManager manager;  // The class under test.

  std::optional<ClientLoginData> loginData =
      makeLoginData(42, "users", "10.1.2.3");  // Complete login data.
  loginData->username = "doctor.who";          // Expected subject.
  loginData->lastLogoutTime =
      "2026-01-01 00:00:00 +0000";  // Expected last-logout value.
  loginData->group = "users";       // Expected group.

  REQUIRE(manager.generateToken(loginData));  // Generate the token.
  REQUIRE(loginData->token.has_value());  // Prove generation produced a token
                                          // before decoding it.

  const auto decoded = jwt::decode<jwt::traits::kazuho_picojson>(
      loginData->token.value());  // Decode the token text into claim accessors.

  CHECK(decoded.get_issuer() ==
        kIssuer);  // Prove the issuer claim matches the configured issuer.
  CHECK(decoded.get_type() == kType);  // Prove the type claim matches.
  CHECK(decoded.get_subject() ==
        "doctor.who");  // Prove the user we supplied became the subject.
  CHECK(decoded.get_id() ==
        "42");  // Prove the client id was stored as a string claim.
  CHECK(decoded.get_payload_claim("ip_address").as_string() ==
        "10.1.2.3");  // Prove the IP was embedded as a private claim.
  CHECK(decoded.get_payload_claim("llodt").as_string() ==
        "2026-01-01 00:00:00 +0000");  // Prove the last-logout time was
                                       // embedded.
  CHECK(decoded.get_payload_claim("group").as_string() ==
        "users");                           // Prove the group was embedded.
  CHECK(decoded.has_payload_claim("exp"));  // Prove an expiry claim exists.
  CHECK(decoded.has_payload_claim("iat"));  // Prove an issued-at claim exists.

  // Expiry is ~TOKEN_VALIDITY minutes after issuance.
  const auto expiry = decoded.get_expires_at();  // Read the expiry time point.
  const auto issued =
      decoded.get_issued_at();  // Read the issued-at time point.
  const auto delta = std::chrono::duration_cast<std::chrono::minutes>(
      expiry - issued);  // Convert the gap to whole minutes.
  CHECK(delta.count() == kValidityMinutes);  // Prove the configured validity
                                             // was applied exactly.
}

TEST_CASE("TokenManager token signature round-trips with the configured secret",
          "[gatekeeper][token]")  // Registers the test that only the correct
                                  // secret verifies the signature.
{
  TokenManager manager;  // The class under test.

  std::optional<ClientLoginData> loginData =
      makeLoginData();                      // Build default login data.
  loginData->username       = "roundtrip";  // Set the subject.
  loginData->lastLogoutTime = "llodt";      // Set a last-logout value.
  loginData->group          = "users";      // Set the group.

  REQUIRE(manager.generateToken(loginData));  // Generate a signed token.

  // Verify with the same parameters the manager signed with.
  auto verifier = jwt::verify<jwt::traits::kazuho_picojson>()
                      .allow_algorithm(jwt::algorithm::hs256{kSecret})
                      .with_issuer(kIssuer)
                      .with_type(kType);  // Build a verifier using the
                                          // expected secret, issuer and type.
  auto decoded  = jwt::decode<jwt::traits::kazuho_picojson>(
      loginData->token.value());  // Decode the token.
  REQUIRE_NOTHROW(verifier.verify(
      decoded));  // Prove the correct verifier accepts the token.

  // A verifier with a different secret must reject it.
  auto wrongVerifier =
      jwt::verify<jwt::traits::kazuho_picojson>().allow_algorithm(
          jwt::algorithm::hs256{
              "wrong-secret"});  // Same algorithm but a different key.
  REQUIRE_THROWS_AS(wrongVerifier.verify(decoded),
                    std::exception);  // Prove a wrong secret is rejected, which
                                      // is what makes the signature meaningful.
}

TEST_CASE(
    "TokenManager fails cleanly when login data is incomplete",
    "[gatekeeper][token]")  // Registers the test for a missing required field.
{
  TokenManager manager;  // The class under test.

  std::optional<ClientLoginData> loginData =
      makeLoginData();          // Build otherwise-valid login data.
  loginData->username.reset();  // generateToken dereferences username --
                                // remove it to trigger a clean failure.

  REQUIRE_FALSE(
      manager.generateToken(loginData));  // Prove generation fails rather than
                                          // crashing on the missing field.
  // No token was set.
  REQUIRE_FALSE(loginData->token.has_value());  // Prove no partial token was
                                                // left behind on failure.
}

TEST_CASE(
    "TokenManager validates a well-formed token whose session fields match",
    "[gatekeeper][token]")  // Registers the positive validation case.
{
  TokenManager manager;  // The class under test.

  // isTokenValid() consults the database for the last logout time; with no
  // database the caller-provided fields decide the outcome (documented in
  // tests/README.md). The token check itself (signature, issuer, type,
  // subject, id, ip, group) is exercised here.
  const auto token = valhalla_test::
      makeToken(kSecret,
                kIssuer,
                kType,
                "alice",
                7,
                "users",
                "127.0.0.1",
                "2026-01-01 00:00:00 +0000");  // Sign a token with the test
                                               // secret and fully matching
                                               // claims.
  auto loginData = std::optional<ClientLoginData>(
      makeLoginData(7,
                    "users",
                    "127.0.0.1"));  // Login data for the same client and IP.
  loginData->token    = token;      // Attach the token.
  loginData->username = "alice";    // Matching subject.
  loginData->lastLogoutTime =
      "2026-01-01 00:00:00 +0000";  // Matching last-logout time.

  std::string message;  // Receives any error text.
  CHECK(manager.isTokenValid(loginData,
                             message));  // Prove a consistent, correctly signed
                                         // token is accepted.
  CHECK(message.empty());  // Prove no error message was produced on success.
}

TEST_CASE("TokenManager rejects a token signed with the wrong secret",
          "[gatekeeper][token]")  // Registers the test that a foreign signature
                                  // is refused.
{
  TokenManager manager;  // The class under test.

  auto loginData = std::optional<ClientLoginData>(
      makeLoginData(7, "users", "127.0.0.1"));  // Login data for client 7.
  loginData->token =
      valhalla_test::makeToken("intruder-secret",
                               kIssuer,
                               kType,
                               "alice",
                               7,
                               "users",
                               "127.0.0.1",
                               "llodt");  // Token signed with a different key.
  loginData->username = "alice";  // Other fields are consistent, so only the
                                  // signature is wrong.
  loginData->lastLogoutTime = "llodt";  // Matching last-logout claim.

  std::string message;  // Receives the error text.
  REQUIRE_FALSE(
      manager
          .isTokenValid(loginData,
                        message));  // Prove the wrong-secret token is rejected.
  CHECK_FALSE(message.empty());  // Prove a reason is given for the rejection.
}

TEST_CASE("TokenManager rejects a tampered token",
          "[gatekeeper][token]")  // Registers the test that altered content is
                                  // detected.
{
  TokenManager manager;  // The class under test.

  auto loginData = std::optional<ClientLoginData>(
      makeLoginData(7, "users", "127.0.0.1"));  // Login data for client 7.
  const auto goodToken =
      valhalla_test::makeToken(kSecret,
                               kIssuer,
                               kType,
                               "alice",
                               7,
                               "users",
                               "127.0.0.1",
                               "llodt");  // Start with a valid token.
  loginData->token = valhalla_test::makeTamperedToken(
      goodToken);  // Alter it so the signature no longer matches the content.
  loginData->username       = "alice";  // Other fields stay consistent.
  loginData->lastLogoutTime = "llodt";  // Matching last-logout claim.

  std::string message;  // Receives the error text.
  REQUIRE_FALSE(manager.isTokenValid(loginData,
                                     message));  // Prove tampering is rejected.
  CHECK_FALSE(message.empty());                  // Prove a reason is given.
}

TEST_CASE("TokenManager rejects a malformed token without crashing",
          "[gatekeeper][token]")  // Registers the robustness test for
                                  // unparseable token strings.
{
  TokenManager manager;  // The class under test.

  const auto& malformed =
      GENERATE(std::string(""),
               std::string("not-a-token"),
               std::string("a.b"),
               std::string("..."),
               std::string(
                   // Run once per broken string: empty, junk, too few segments,
                   // only dots, and bad base64.
                   "eyJhbGciOiJIUzI1NiJ9.e30.!!!invalid-base64!!!"));

  auto loginData = std::optional<ClientLoginData>(
      makeLoginData());          // Fresh login data for each run.
  loginData->token = malformed;  // Install the malformed token.
  std::string message;           // Receives the error text.

  CAPTURE(malformed);  // Print the offending string if an assertion fails.
  REQUIRE_FALSE(
      manager.isTokenValid(loginData,
                           message));  // Prove every malformed token is
                                       // rejected rather than crashing.
  CHECK_FALSE(message.empty());        // Prove a reason is given.
}
TEST_CASE("TokenManager generateToken fails cleanly for each missing field",
          "[gatekeeper][token]")  // Registers the tests that each required
                                  // claim is genuinely required.
{
  TokenManager manager;  // The class under test.

  SECTION("missing clientId")
  {
    auto loginData = std::optional<ClientLoginData>(
        makeLoginData(42, "users", "1.2.3.4"));  // Start with complete data.
    loginData->username       = "u";             // Set the username.
    loginData->lastLogoutTime = "llodt";         // Set last logout.
    loginData->group          = "users";         // Set group.
    loginData->clientId.reset();  // Remove the client id, which is needed
                                  // as the token subject.

    REQUIRE_FALSE(manager.generateToken(
        loginData));  // Prove generation fails without it.
    CHECK_FALSE(loginData->token.has_value());  // Prove no token was produced.
  }

  SECTION("missing ip_address")
  {
    auto loginData = std::optional<ClientLoginData>(
        makeLoginData(42, "users", "1.2.3.4"));  // Complete data.
    loginData->username       = "u";             // Set username.
    loginData->lastLogoutTime = "llodt";         // Set last logout.
    loginData->group          = "users";         // Set group.
    loginData->ip_address.reset();               // Remove the IP claim.

    REQUIRE_FALSE(
        manager.generateToken(loginData));  // Prove the IP is required.
  }

  SECTION("missing group")
  {
    auto loginData = std::optional<ClientLoginData>(
        makeLoginData(42, "users", "1.2.3.4"));  // Complete data.
    loginData->username       = "u";             // Set username.
    loginData->lastLogoutTime = "llodt";         // Set last logout.
    loginData->group.reset();                    // Remove the group claim.

    REQUIRE_FALSE(
        manager.generateToken(loginData));  // Prove the group is required.
  }

  SECTION("missing lastLogoutTime")
  {
    auto loginData = std::optional<ClientLoginData>(
        makeLoginData(42, "users", "1.2.3.4"));  // Complete data.
    loginData->username = "u";                   // Set username.
    loginData->group    = "users";               // Set group.
    loginData->lastLogoutTime.reset();  // Remove the last-logout claim.

    REQUIRE_FALSE(manager.generateToken(
        loginData));  // Prove the last-logout time is required.
  }
}

TEST_CASE("TokenManager rejects an already-expired token",
          "[gatekeeper][token]")  // Registers the test that expiry is checked
                                  // during validation.
{
  TokenManager manager;  // The class under test.

  // Build a token that expired 1 minute ago.
  const auto token =
      valhalla_test::makeToken(kSecret,
                               kIssuer,
                               kType,
                               "alice",
                               7,
                               "users",
                               "127.0.0.1",
                               "llodt",
                               std::chrono::minutes{
                                   -1});  // Negative validity puts the expiry
                                          // in the past.

  auto loginData = std::optional<ClientLoginData>(
      makeLoginData(7, "users", "127.0.0.1"));  // Login data for client 7.
  loginData->token = token;                     // Attach the expired token.
  loginData->username =
      "alice";  // Other fields match, so expiry is the only problem.
  loginData->lastLogoutTime = "llodt";  // Matching last-logout claim.

  std::string message;  // Receives the error text.
  REQUIRE_FALSE(
      manager.isTokenValid(loginData,
                           message));  // Prove the expired token is rejected.
  CHECK_FALSE(message.empty());  // Prove the reason is reported to the caller.
}
