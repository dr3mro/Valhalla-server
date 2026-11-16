/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file test_keeprbase.cpp
 * @brief Catch2 unit tests and fixtures for keeprbase. Checks the shared base
 * class behind the keepers: time formatting, verifier creation, claim decoding
 * and last-logout lookup.
 * @details A small TestKeepr subclass exposes KeeprBase's protected helpers
 * with using-declarations so they can be called directly. The tests verify the
 * UTC timestamp format, that token settings come from the test environment,
 * that a verifier accepts a good token but throws on incomplete data, that
 * decodeToken extracts claims and rejects an IP mismatch and an expired token,
 * and that a null database result returns false cleanly (the bug-4 regression).
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It protects keeprbase.hpp, the shared foundation of the token and session
 * keepers. These helpers format the timestamps embedded in JWTs and decide
 * whether a token's claims are acceptable, so a mistake could issue tokens that
 * never verify or quietly accept tokens that should be refused. The nullopt
 * case also guards against dereferencing an empty optional, a crash that was
 * fixed under bug 4.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE / SECTION:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Catch2 CHECK / REQUIRE / CHECK_THROWS_AS / REQUIRE_NOTHROW assertion
 * macros: https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - JSON Web Tokens: header, payload, claims and signature:
 * https://jwt.io/introduction
 * - std::optional ("a value that may or may not be present"):
 * https://en.cppreference.com/w/cpp/utility/optional
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Provides TEST_CASE, SECTION and the CHECK/REQUIRE family of macros.
#include <catch2/catch_test_macros.hpp>
// std::chrono::minutes, used to build an already-expired test token.
#include <chrono>
// std::optional / std::nullopt, the login-data and DB-result types.
#include <optional>
// std::regex / regex_match, used to check the timestamp string's shape.
#include <regex>
#include <string>  // std::string, for tokens, secrets and timestamp text.

// The production header under test: api::v2::KeeprBase.
#include "gatekeeper/keeprbase/keeprbase.hpp"
// Shared gatekeeper types, including ClientLoginData.
#include "gatekeeper/types.hpp"
// makeLoginData / makeToken, which build consistent test tokens.
#include "support/builders.hpp"
// FakeDatabaseController / FakeDbGuard, used in the final test.
#include "support/fake_db.hpp"

namespace
{
  /** @brief Exposes KeeprBase's protected helpers so they can be tested
   * directly.
   *
   * No behaviour is overridden; the `using` declarations only widen access.
   */
  struct TestKeepr : api::v2::KeeprBase
  {
    using KeeprBase::createTokenVerifier;         // Make the protected verifier
                                                  // factory public for testing.
    using KeeprBase::current_time_to_utc_string;  // Make the protected time
                                                  // formatter public for
                                                  // testing.
    using KeeprBase::decodeToken;  // Make the protected claim decoder
                                   // public for testing.
    using KeeprBase::getTokenManagerParameters;  // Make the protected
                                                 // config getter public for
                                                 // testing.
    using KeeprBase::setNowLoginTimeGetLastLogoutTime;  // Make the protected
                                                        // DB-backed helper
                                                        // public for testing.
  };

  constexpr const char* kSecret =
      "valhalla-unit-test-secret-key";  // Signing secret; must match the test
                                        // environment's setting.
  constexpr const char* kIssuer =
      "valhalla-unit-test-issuer";  // Expected token issuer; also from the
                                    // test environment.
  constexpr const char* kType =
      "JWS";  // Expected token type string used when signing test tokens.
}  // namespace

TEST_CASE("KeeprBase::current_time_to_utc_string returns a UTC timestamp",
          "[gatekeeper][keeprbase]")  // Registers the test that the formatter
                                      // produces the exact text shape expected
                                      // inside tokens.
{
  const std::string ts =
      TestKeepr::current_time_to_utc_string();  // Ask the helper for the
                                                // current time as a string.

  REQUIRE_FALSE(
      ts.empty());  // Prove something was produced before trying to match it.
  // Expected format: "YYYY-MM-DD HH:MM:SS +0000"
  const std::regex utcPattern(
      R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} \+0000)");  // Pattern:
                                                         // four-digit year,
                                                         // two-digit
                                                         // month/day, time,
                                                         // then the UTC
                                                         // offset.
  CHECK(std::regex_match(ts,
                         utcPattern));  // Prove the whole string matches;
                                        // otherwise downstream parsers could
                                        // misread the timestamp.
}

TEST_CASE("KeeprBase::current_time_to_utc_string ends with '+0000'",
          "[gatekeeper][keeprbase]")  // Registers a second, simpler check on
                                      // the UTC suffix.
{
  const std::string ts =
      TestKeepr::current_time_to_utc_string();  // Get another timestamp from
                                                // the helper.
  CHECK(ts.size() >
        5);  // Prove it is long enough to contain a five-character suffix.
  CHECK(ts.substr(ts.size() - 5) ==
        "+0000");  // Take the last five characters and prove they are the UTC
                   // offset marker.
}

TEST_CASE(
    "KeeprBase::getTokenManagerParameters returns values matching the test "
    "environment",
    "[gatekeeper][keeprbase]")  // Registers the test that config is read from
                                // environment variables.
{
  TestKeepr k;  // Instance used to call the protected getter.
  const Configurator::TokenManagerParameters params =
      k.getTokenManagerParameters();  // Fetch the settings the token manager
                                      // will use.

  // The test environment (support/global_env.cpp) sets these env vars.
  CHECK(params.secret ==
        kSecret);  // Prove the secret is the one the test setup exported.
  CHECK(params.issuer ==
        kIssuer);  // Prove the issuer matches the test environment.
  CHECK(params.type == kType);  // Prove the token type matches too.
  CHECK(params.validity > 0);   // Prove a positive lifetime was configured, so
                                // issued tokens have a sensible expiry.
}

TEST_CASE(
    "KeeprBase::createTokenVerifier constructs a verifier that accepts a valid "
    "token",
    "[gatekeeper][keeprbase]")  // Registers the test that a correctly signed
                                // token can be verified.
{
  TestKeepr         k;  // Instance used to reach the protected factory.
  const std::string llodt =
      "2026-01-01 00:00:00 +0000";  // Fixed last-logout time embedded in the
                                    // token.
  std::optional<api::v2::Types::ClientLoginData> data =
      valhalla_test::makeLoginData(500,
                                   "users",
                                   "127.0.0.1");  // Build consistent login data
                                                  // for client 500.
  data->username       = "testuser";  // Username matching the token's subject.
  data->lastLogoutTime = llodt;  // Last-logout time matching the token claim.
  data->token = valhalla_test::makeToken(kSecret,
                                         kIssuer,
                                         kType,
                                         "testuser",
                                         500,
                                         "users",
                                         "127.0.0.1",
                                         llodt);  // Sign a token with the test
                                                  // secret and issuer.

  // createTokenVerifier must not throw for valid data.
  REQUIRE_NOTHROW(
      [&]
      {
        (void)k.createTokenVerifier(data);
      }());  // Wrap in a lambda because the macro needs one expression; prove
             // valid data does not cause an exception.

  // The verifier should accept the token we just created.
  auto verifier = k.createTokenVerifier(
      data);  // Build the verifier from the same login data.
  auto decodedToken = jwt::decode<jwt::traits::kazuho_picojson>(
      data->token.value());  // Decode the token text into a claim object.
  CHECK_NOTHROW(verifier.verify(
      decodedToken));  // Prove the verifier accepts this token; a throw would
                       // mean valid tokens are being rejected.
}

TEST_CASE("KeeprBase::createTokenVerifier throws when login data is incomplete",
          "[gatekeeper][keeprbase]")  // Registers the test that incomplete data
                                      // is rejected loudly.
{
  TestKeepr k;  // Instance used to reach the protected factory.

  SECTION("empty optional")
  {
    std::optional<api::v2::Types::ClientLoginData>
        empty;  // No login data at all.
    CHECK_THROWS_AS(k.createTokenVerifier(empty),
                    std::runtime_error);  // Prove the factory refuses it with a
                                          // clear exception rather than
                                          // returning a broken verifier.
  }

  SECTION("missing username")
  {
    std::optional<api::v2::Types::ClientLoginData> data =
        valhalla_test::makeLoginData(501,
                                     "users");  // Build data that is complete
                                                // except for the username.
    data->username.reset();  // Remove the username, which the verifier
                             // needs as the expected subject.
    CHECK_THROWS_AS(k.createTokenVerifier(data),
                    std::runtime_error);  // Prove the missing required field
                                          // triggers an exception.
  }
}

TEST_CASE("KeeprBase::decodeToken extracts claims from a valid JWT",
          "[gatekeeper][keeprbase]")  // Registers the test that claims are
                                      // copied out of a good token.
{
  const std::string llodt =
      "2026-01-01 00:00:00 +0000";  // Fixed last-logout time to embed as a
                                    // claim.
  std::optional<api::v2::Types::ClientLoginData> data =
      valhalla_test::makeLoginData(600,
                                   "providers",
                                   "10.0.0.1");  // Login data for a provider.
  data->username       = "drsmith";  // Subject that the token will also carry.
  data->lastLogoutTime = llodt;      // Matching last-logout claim.
  data->token = valhalla_test::makeToken(kSecret,
                                         kIssuer,
                                         kType,
                                         "drsmith",
                                         600,
                                         "providers",
                                         "10.0.0.1",
                                         llodt);  // Sign a well-formed token.

  auto decoded = jwt::decode<jwt::traits::kazuho_picojson>(
      data->token.value());  // Decode the token into its claim object.

  std::string message;  // Receives any validation error text.
  const bool  ok =
      TestKeepr::decodeToken(data,
                             message,
                             decoded);  // Let the helper fill 'data' from the
                                        // token's claims.

  REQUIRE(ok);  // Prove decoding succeeded before we inspect the populated
                // fields.
  CHECK(message.empty());                // Prove no error message was produced.
  REQUIRE(data->clientId.has_value());   // Prove the id claim was actually
                                         // written before we read it.
  CHECK(data->clientId.value() == 600);  // Prove the id was copied correctly.
  CHECK(data->group.value() == "providers");  // Prove the group claim survived.
  CHECK(data->username.value() ==
        "drsmith");  // Prove the subject claim survived.
}

TEST_CASE("KeeprBase::decodeToken rejects a mismatched IP address",
          "[gatekeeper][keeprbase]")  // Registers the test that a token is
                                      // bound to the IP it was issued for.
{
  const std::string llodt =
      "2026-01-01 00:00:00 +0000";  // Fixed last-logout time.
  // Token is issued for IP 192.168.1.1 but data claims 192.168.1.2.
  std::optional<api::v2::Types::ClientLoginData> data =
      valhalla_test::makeLoginData(601,
                                   "users",
                                   "192.168.1.2");  // The request claims a
                                                    // different address.
  data->username       = "user";  // Subject matches the token.
  data->lastLogoutTime = llodt;   // Last-logout time matches the token.
  data->token =
      valhalla_test::makeToken(kSecret,
                               kIssuer,
                               kType,
                               "user",
                               601,
                               "users",
                               "192.168.1.1",
                               llodt);  // Token was signed for 192.168.1.1.

  auto decoded = jwt::decode<jwt::traits::kazuho_picojson>(
      data->token
          .value());  // Decode the token (decoding does not verify the IP).

  std::string message;  // Receives the rejection reason.
  CHECK_FALSE(
      TestKeepr::decodeToken(data, message, decoded));  // Prove the IP mismatch
                                                        // causes rejection.
  CHECK(message.find("ip address") !=
        std::string::npos);  // Prove the message actually mentions the IP, so
                             // callers get a useful clue.
}

TEST_CASE("KeeprBase::decodeToken rejects an expired token",
          "[gatekeeper][keeprbase]")  // Registers the test that expiry is
                                      // enforced during decode.
{
  const std::string llodt =
      "2026-01-01 00:00:00 +0000";  // Fixed last-logout time.
  std::optional<api::v2::Types::ClientLoginData> data =
      valhalla_test::makeLoginData(602,
                                   "users",
                                   "127.0.0.1");  // Otherwise-valid login data.
  data->username       = "user";                  // Subject matching the token.
  data->lastLogoutTime = llodt;  // Last-logout time matching the token.
  // Token that expired 1 minute ago.
  data->token = valhalla_test::makeToken(kSecret,
                                         kIssuer,
                                         kType,
                                         "user",
                                         602,
                                         "users",
                                         "127.0.0.1",
                                         llodt,
                                         std::chrono::minutes{
                                             -1});  // Negative validity places
                                                    // the expiry in the past.

  auto decoded = jwt::decode<jwt::traits::kazuho_picojson>(
      data->token.value());  // Decode the expired token.

  std::string message;  // Receives the rejection reason.
  CHECK_FALSE(
      TestKeepr::decodeToken(data,
                             message,
                             decoded));  // Prove an expired token is rejected.
  CHECK(message.find("expired") !=
        std::string::npos);  // Prove the reason explicitly says it expired.
}

// Bug 4 fixed: the .value() call on an empty optional in the error branch has
// been removed; the function now returns false cleanly when the DB returns
// nullopt.
TEST_CASE(
    "KeeprBase::setNowLoginTimeGetLastLogoutTime returns false when DB returns "
    "nullopt",
    "[gatekeeper][keeprbase]")  // Registers the regression test for the
                                // empty-DB-result crash.
{
  // Create the fake DB controller.
  auto fake          = std::make_shared<FakeDatabaseController>();
  fake->query_result = std::nullopt;  // Make it report "no row found", the
                                      // condition that used to crash.
  FakeDbGuard guard(fake);            // Inject the fake before any keeper is
                                      // constructed, so it binds to this fake.

  TestKeepr k;  // Instance used to reach the protected helper.
  auto      data = std::make_optional<
      api::v2::Types::ClientLoginData>();  // A real (non-empty) data object
                                           // to pass in.
  data->group    = std::string("users");   // Group needed to form the DB query.
  data->clientId = uint64_t{42};  // Client id needed to form the DB query.

  bool result = k.setNowLoginTimeGetLastLogoutTime(
      data);            // Call the helper with a null DB result.
  CHECK_FALSE(result);  // Prove it returns false cleanly instead of throwing
                        // on an empty optional.
}
