/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file test_keeprbase_validatetoken.cpp
 * @brief Catch2 unit tests and fixtures for keeprbase validatetoken. Checks
 * KeeprBase::validateToken against a fake database.
 * @details validateToken is protected, so the tiny TestKeeprValidate subclass
 * exposes it with a using-declaration. The tests feed it session rows from a
 * fake DB to prove that a last-logout mismatch or a suspended account is
 * rejected, that a missing session row falls through to caller-set values
 * (documented current behaviour), that a tampered token can never succeed, and
 * that a fully valid active token passes with no error message.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It protects validateToken in keeprbase.hpp, the check that ties a JWT to the
 * account state stored in the database. If it accepted a token after a logout
 * or for a suspended account, revoked access would keep working; if it rejected
 * a valid session, legitimate users would be locked out. Pinning all these
 * branches down makes the security boundary explicit and guards against silent
 * changes in a high-risk area.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Catch2 CHECK / CHECK_FALSE / SUCCEED assertion macros:
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - JSON Web Tokens (claims, signatures and tampering):
 * https://jwt.io/introduction
 * - std::optional ("a value that may or may not be present"):
 * https://en.cppreference.com/w/cpp/utility/optional
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Tests for KeeprBase::validateToken via FakeDatabaseController.
// validateToken is protected; exposed through TestKeeprValidate below.
// Provides TEST_CASE and the CHECK / CHECK_FALSE / SUCCEED macros.
#include <catch2/catch_test_macros.hpp>
// Included for time-related helpers used when building tokens.
#include <chrono>
// jsoncons::json, the shape of a fake database row.
#include <jsoncons/json.hpp>
// std::optional / std::nullopt for login data and DB results.
#include <optional>
#include <string>  // std::string for secrets, messages and timestamps.

// The production header under test: api::v2::KeeprBase.
#include "gatekeeper/keeprbase/keeprbase.hpp"
// makeLoginData / makeToken / makeTamperedToken test helpers.
#include "support/builders.hpp"
// FakeDatabaseController and FakeDbGuard for hermetic DB answers.
#include "support/fake_db.hpp"

namespace
{
  /** @brief Exposes the protected validateToken so it can be driven with a
   * fake DB. */
  struct TestKeeprValidate : api::v2::KeeprBase
  {
    using KeeprBase::validateToken;  // Widen access to the protected
                                     // function without changing its
                                     // behaviour.
  };

  constexpr const char* kSecret =
      "valhalla-unit-test-secret-key";  // Signing secret, matching
                                        // support/global_env.cpp.
  constexpr const char* kIssuer =
      "valhalla-unit-test-issuer";      // Expected issuer from the test
                                        // environment.
  constexpr const char* kType = "JWS";  // Expected token type string.

  /**
   * Builds a JSON row that getLastLogoutTimeIfActive() expects:
   * { "id": <id>, "last_logout": <llodt>, "active": <active> }
   */
  jsoncons::json        makeSessionRow(uint64_t id, const std::string& llodt,
      bool active)  // Builds one fake DB row describing a session.
  {
    jsoncons::json row =
        jsoncons::json::object();  // Start with an empty JSON object rather
                                   // than an array.
    row["id"] = id;  // Client id, used by the helper to identify the session.
    row["last_logout"] = llodt;   // Last-logout timestamp that must agree
                                  // with the token's claim.
    row["active"]      = active;  // Whether the account is currently enabled.
    return row;  // Return the fully built row for the fake to hand back.
  }
}  // namespace

TEST_CASE(
    "KeeprBase::validateToken returns false when llodt mismatches DB value",
    "[gatekeeper][keeprbase][validatetoken]")  // Registers the test that a
                                               // token issued before a logout
                                               // is rejected.
{
  auto fake =
      std::make_shared<FakeDatabaseController>();  // Create the fake DB.
  FakeDbGuard       guard(fake);  // Inject it before any keeper is built.

  const std::string tokenLlodt =
      "2026-01-01 00:00:00 +0000";  // Last-logout time embedded in the token.
  const std::string dbLlodt =
      "2026-06-15 12:00:00 +0000";  // different -- a later logout stored in
                                    // the database.

  std::optional<api::v2::Types::ClientLoginData> data =
      valhalla_test::makeLoginData(
          700, "users", "127.0.0.1");  // Login data for client 700.
  data->username       = "user";       // Subject matching the token.
  data->lastLogoutTime = tokenLlodt;   // Caller's claim, matching the token.
  data->token = valhalla_test::makeToken(kSecret, kIssuer, kType, "user", 700,
      "users", "127.0.0.1",
      tokenLlodt);  // Sign the token with the older timestamp.

  // DB returns a different llodt → validation must fail.
  fake->read_result = makeSessionRow(700, dbLlodt,
      true);  // Fake reports a newer logout, so this token is stale.

  TestKeeprValidate k;  // Instance used to reach the protected function.
  auto              decoded = jwt::decode<jwt::traits::kazuho_picojson>(
      data->token.value());  // Decode the token to its claims.
  std::string message;       // Receives the rejection reason.
  CHECK_FALSE(k.validateToken(
      data, message, decoded));  // Prove the stale token is rejected.
  CHECK(message.find("logged out") !=
        std::string::npos);  // Prove the reason mentions logout, which is the
                             // actual cause.
}

TEST_CASE("KeeprBase::validateToken returns false when account is inactive",
    "[gatekeeper][keeprbase][validatetoken]")  // Registers the test that a
                                               // suspended account is blocked.
{
  auto fake =
      std::make_shared<FakeDatabaseController>();  // Create the fake DB.
  FakeDbGuard       guard(fake);  // Inject it before the keeper is built.

  const std::string llodt =
      "2026-01-01 00:00:00 +0000";  // Timestamp that will match on both
                                    // sides.

  std::optional<api::v2::Types::ClientLoginData> data =
      valhalla_test::makeLoginData(
          701, "users", "127.0.0.1");  // Login data for client 701.
  data->username       = "user";       // Subject matching the token.
  data->lastLogoutTime = llodt;        // Matching last-logout claim.
  data->token = valhalla_test::makeToken(kSecret, kIssuer, kType, "user", 701,
      "users", "127.0.0.1", llodt);  // Sign a valid token.

  // llodt matches but account is suspended.
  fake->read_result = makeSessionRow(701, llodt, /*active=*/
      false);  // Same timestamp but active=false, so access should be denied.

  TestKeeprValidate k;  // Instance used to reach the protected function.
  auto              decoded = jwt::decode<jwt::traits::kazuho_picojson>(
      data->token.value());  // Decode the token.
  std::string message;       // Receives the rejection reason.
  CHECK_FALSE(k.validateToken(
      data, message, decoded));  // Prove the inactive account is rejected.
  CHECK(message.find("suspend") !=
        std::string::npos);  // Prove the reason points at the suspension.
}

TEST_CASE(
    "KeeprBase::validateToken proceeds with caller-set values when DB returns "
    "no row",
    "[gatekeeper][keeprbase][validatetoken]")  // Registers the test documenting
                                               // behaviour when the session row
                                               // is missing.
{
  // validateToken ignores the bool returned by getLastLogoutTimeIfActive(),
  // so when the DB returns nullopt it falls through using whatever the caller
  // pre-set in clientLoginData. With llodt and active matching, it succeeds.
  // This documents the production behaviour: missing session row is not an
  // automatic rejection.
  auto fake =
      std::make_shared<FakeDatabaseController>();  // Create the fake DB.
  FakeDbGuard       guard(fake);  // Inject it before the keeper is built.

  const std::string llodt =
      "2026-01-01 00:00:00 +0000";  // Timestamp used for both token and
                                    // caller data.

  std::optional<api::v2::Types::ClientLoginData> data =
      valhalla_test::makeLoginData(
          702, "users", "127.0.0.1");  // Login data for client 702.
  data->username       = "user";       // Subject matching the token.
  data->lastLogoutTime = llodt;        // Caller-supplied last-logout time.
  data->is_active      = true;  // Caller-supplied active flag; with no DB row
                                // these decide the outcome.
  data->token = valhalla_test::makeToken(kSecret, kIssuer, kType, "user", 702,
      "users", "127.0.0.1", llodt);  // Sign the token.

  // DB returns nothing.
  fake->read_result = std::nullopt;  // Simulate a missing session row.

  TestKeeprValidate k;  // Instance used to reach the protected function.
  auto              decoded = jwt::decode<jwt::traits::kazuho_picojson>(
      data->token.value());  // Decode the token.
  std::string message;       // Receives any error text.
  // Should succeed because pre-set llodt matches token and is_active == true.
  CHECK(k.validateToken(data, message,
      decoded));  // Prove the missing row does not cause an automatic
                  // rejection.
}

TEST_CASE(
    "KeeprBase::validateToken returns false for a structurally malformed token",
    "[gatekeeper][keeprbase][validatetoken]")  // Registers the test that
                                               // tampering can never lead to
                                               // acceptance.
{
  auto fake =
      std::make_shared<FakeDatabaseController>();  // Create the fake DB.
  FakeDbGuard       guard(fake);  // Inject it before the keeper is built.

  const std::string llodt =
      "2026-01-01 00:00:00 +0000";  // Timestamp used in the good token.

  std::optional<api::v2::Types::ClientLoginData> data =
      valhalla_test::makeLoginData(
          703, "users", "127.0.0.1");  // Login data for client 703.
  data->username       = "user";       // Subject matching the token.
  data->lastLogoutTime = llodt;        // Matching last-logout time.

  // A token with a tampered signature.
  std::string good = valhalla_test::makeToken(kSecret, kIssuer, kType, "user",
      703, "users", "127.0.0.1", llodt);  // First create a genuine token.
  data->token      = valhalla_test::makeTamperedToken(
      good);  // Corrupt it so signature verification should fail.

  fake->read_result = makeSessionRow(
      703, llodt, true);  // The DB side is otherwise perfectly valid.

  TestKeeprValidate k;  // Instance used to reach the protected function.
  // jwt::decode on a tampered token may throw or produce a decodable token
  // that fails verify(). Either way validateToken must not succeed.
  try
  {
    auto decoded = jwt::decode<jwt::traits::kazuho_picojson>(
        data->token.value());  // Try to decode the tampered text.
    std::string message;       // Receives any error text.
    CHECK_FALSE(k.validateToken(data, message,
        decoded));  // If decoding worked, prove validation still rejects
                    // it.
  }
  catch (...)
  {
    // decode itself threw — that's also acceptable (token was malformed).
    SUCCEED("jwt::decode threw for tampered token");  // A throw from decode is
                                                      // a valid outcome; mark
                                                      // the test as passing
                                                      // deliberately.
  }
}

TEST_CASE("KeeprBase::validateToken returns true for a valid active token",
    "[gatekeeper][keeprbase][validatetoken]")  // Registers the positive case
                                               // for a healthy session.
{
  auto fake =
      std::make_shared<FakeDatabaseController>();  // Create the fake DB.
  FakeDbGuard       guard(fake);  // Inject it before the keeper is built.

  const std::string llodt =
      "2026-01-01 00:00:00 +0000";  // Timestamp used on both sides.

  std::optional<api::v2::Types::ClientLoginData> data =
      valhalla_test::makeLoginData(
          704, "users", "127.0.0.1");  // Login data for client 704.
  data->username       = "user";       // Subject matching the token.
  data->lastLogoutTime = llodt;        // Matching last-logout claim.
  data->token = valhalla_test::makeToken(kSecret, kIssuer, kType, "user", 704,
      "users", "127.0.0.1", llodt);  // Sign a valid token.

  // llodt matches, account is active.
  fake->read_result = makeSessionRow(
      704, llodt, true);  // Fake reports a matching, active session.

  TestKeeprValidate k;  // Instance used to reach the protected function.
  auto              decoded = jwt::decode<jwt::traits::kazuho_picojson>(
      data->token.value());  // Decode the token.
  std::string message;       // Receives any error text.
  CHECK(k.validateToken(
      data, message, decoded));  // Prove a fully valid token is accepted.
  CHECK(message.empty());  // Prove no misleading error message was produced
                           // on success.
}
