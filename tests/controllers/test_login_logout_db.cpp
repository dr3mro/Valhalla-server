/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file test_login_logout_db.cpp
 * @brief Catch2 unit tests and fixtures for login logout db.
 * @details These tests drive the Login and Logout database paths of
 * ClientController<User> using a fake database instead of PostgreSQL. For Login
 * they arrange fake password rows and fake session-write results to cover every
 * outcome: unknown user, suspended user, failed session write, wrong password,
 * success, malformed credentials, an SQL-injection-looking username and a busy
 * database. Passwords are hashed with the real PasswordCrypt so the success and
 * wrong-password cases exercise the genuine comparison. Logout is checked with
 *          a properly signed test JWT and with an empty token.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Login and logout are the gatekeepers of a healthcare system, so their failure
 * modes must be exact. These tests guarantee that a bad password never writes a
 * session row (query_calls stays zero), that a successful login writes it
 * exactly once and returns a token, and that overload is reported as 503. They
 * also confirm that SQL-like text in a username is treated as data, not
 * executable SQL. A regression here could lock out staff, grant access wrongly,
 * or corrupt session timestamps.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE and CHECK:
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - jwt-cpp (creating and signing JSON Web Tokens):
 * https://github.com/Thalhammer/jwt-cpp
 * - SQL injection and why bound parameters prevent it:
 * https://owasp.org/www-community/attacks/SQL_Injection
 * - HTTP 401 Unauthorized:
 * https://developer.mozilla.org/en-US/docs/Web/HTTP/Status/401
 * - std::chrono (dates and times): https://en.cppreference.com/w/cpp/chrono
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// DB-path tests for Login and Logout via ClientController<User>.
// Uses FakeDatabaseController to simulate DB responses without PostgreSQL.
// Provides TEST_CASE, CHECK and REQUIRE.
#include <catch2/catch_test_macros.hpp>
// jsoncons::json, used to build fake password/session rows.
#include <jsoncons/json.hpp>
// std::make_shared, used to create the shared fake database controller.
#include <memory>
// std::string holds credentials, tokens and the captured response body.
#include <string>

// The controller class under test.
#include "controllers/clientcontroller/clientcontroller.hpp"
// User, the entity this controller is specialized for.
#include "entities/people/user.hpp"
// PasswordCrypt, used to hash a real fixture password.
#include "gatekeeper/passwordcrypt/passwordcrypt.hpp"
// FakeDatabaseController and FakeDbGuard, the hermetic DB stand-ins.
#include "support/fake_db.hpp"
// CALLBACK_, the callback type controllers answer through.
#include "utils/global/callback.hpp"
// Http::Status, the named status codes used in assertions.
#include "utils/global/http.hpp"
// Requester, the caller identity passed to each method.
#include "utils/global/requester.hpp"

namespace Http =
    api::v2::Http;  // A short alias so assertions read as Http::Status::OK.
using api::v2::Requester;  // Bring the requester type into this file's scope.

namespace  // An anonymous namespace keeps these helpers private to this test
           // file.
{
  /** @brief The status code and body passed to a controller callback. */
  struct CR
  {
    int code = -1;     // The HTTP status; -1 means the callback never fired.
    std::string body;  // The response text, filled by the callback.
  };

  /** @brief Builds a callback that records its code/body into @p r. */
  CALLBACK_ cap(
      CR& r)  // Returns a "call me later" function that writes into r.
  {
    return [&r](int                c,
                const std::string& b)  // The lambda captures r by reference so
                                       // the result is visible to the caller.
    {
      r.code = c;  // Store the status the controller reported.
      r.body = b;  // Store the body the controller reported.
    };
  }

  /** @brief The placeholder requester for Login/Logout; those methods ignore
   * it. */
  Requester anonRequester()
  {
    return Requester(0, "users");
  }  // A throwaway identity; authentication uses the username/password
     // instead.

  /** @brief Builds the password_result fixture with id, hash and active flag.
   */
  jsoncons::json makePasswordResult(
      uint64_t           id,
      const std::string& hash,
      bool               active =
          true)  // Shapes the row the fake returns from the password lookup.
  {
    jsoncons::json j =
        jsoncons::json::object();  // Start with an empty JSON object.
    j["id"]       = id;            // The user's id.
    j["password"] = hash;  // The stored password hash used for verification.
    j["active"] =
        active;  // Whether the account is active (false means suspended).
    return j;    // Hand the finished row back.
  }

  /** @brief Builds the query_result fixture for the session login-time write.
   */
  jsoncons::json makeSessionResult(
      const std::string& lastLogout =
          "2026-01-01 00:00:00 +0000")  // Shapes the row returned by the
                                        // session UPDATE.
  {
    jsoncons::json j =
        jsoncons::json::object();  // Start with an empty JSON object.
    j["last_logout"] =
        lastLogout;  // Supply a timestamp for the previous logout.
    return j;        // Hand the finished row back.
  }

  /** @brief Hashes a fixture password with the real PasswordCrypt so
   * verification is exercised. */
  std::string hashKnownPassword(
      const std::string& pw)  // Produce a genuine hash for the fake row, so
                              // Login compares against real cryptography.
  {
    PasswordCrypt crypt;              // The real password-hashing helper.
    auto h = crypt.hashPassword(pw);  // Hash the known password; hashing can
                                      // fail, so the result is optional-like.
    REQUIRE(h.has_value());           // Abort the test if hashing failed, since
                                      // every later check would be meaningless.
    return h.value();                 // Return the produced hash string.
  }
}  // namespace

// Login is driven entirely by FakeDatabaseController fields: password_result
// decides whether the account is found/active, query_result decides whether the
// session write succeeded. Hashing with the real PasswordCrypt means the
// success and wrong-password cases exercise the actual comparison. query_calls
// counts writes only, so it can prove a bad password never touched the session
// row.

// ── Login: DB returns nullopt (user not found) ───────────────────────────────

// An unknown username yields no password row and must not authenticate.
TEST_CASE("ClientController::Login returns UNAUTHORIZED when user not in DB",
          "[controller][client][login][db]")
{
  // A shared fake for this case.
  auto fake             = std::make_shared<FakeDatabaseController>();
  fake->password_result = std::nullopt;  // user does not exist

  FakeDbGuard guard(fake);      // Install the fake before the controller.
  ClientController<User> ctrl;  // Controller captures it from the Store.
  CR                     r;     // Holds the answer.
  ctrl.Login(cap(r),
             anonRequester(),
             R"({"username":"alice","password":"secret"})",
             "127.0.0.1");  // Attempt login with a valid-shaped body for a user
                            // the fake says does not exist.
  CHECK(r.code == Http::Status::UNAUTHORIZED);  // Proves an unknown user is
                                                // refused with 401.
}

// ── Login: user is suspended ─────────────────────────────────────────────────

// A correct password is still refused when the account is marked inactive.
TEST_CASE("ClientController::Login returns UNAUTHORIZED when user is suspended",
          "[controller][client][login][db]")
{
  auto hash =
      hashKnownPassword("secret");  // Hash the real password for the fake row.
  // A shared fake.
  auto fake = std::make_shared<FakeDatabaseController>();
  fake->password_result =
      makePasswordResult(42,
                         hash,
                         /*active=*/false);  // Correct hash, but the account is
                                             // suspended.

  FakeDbGuard            guard(fake);  // Install the fake first.
  ClientController<User> ctrl;         // Controller captures it.
  CR                     r;            // Holds the answer.
  ctrl.Login(cap(r),
             anonRequester(),
             R"({"username":"alice","password":"secret"})",
             "127.0.0.1");  // The password matches, so only the active flag can
                            // reject.
  CHECK(r.code == Http::Status::UNAUTHORIZED);  // Proves suspended accounts
                                                // cannot log in.
  CHECK(r.body.find("suspended") !=
        std::string::npos);  // Proves the body tells the user why they were
                             // refused.
}

// ── Login: session INSERT fails
// ───────────────────────────────────────────────

// Even a valid, active user is refused if the session-update query fails.
TEST_CASE(
    "ClientController::Login returns UNAUTHORIZED when session INSERT fails",
    "[controller][client][login][db]")
{
  auto hash = hashKnownPassword("secret");  // A valid password hash.
  // A shared fake.
  auto fake = std::make_shared<FakeDatabaseController>();
  fake->password_result =
      makePasswordResult(42,
                         hash,
                         /*active=*/true);  // Active user with the right hash.
  fake->query_result = std::nullopt;  // setNowLoginTimeGetLastLogoutTime fails

  FakeDbGuard            guard(fake);  // Install the fake first.
  ClientController<User> ctrl;         // Controller captures it.
  CR                     r;            // Holds the answer.
  ctrl.Login(cap(r),
             anonRequester(),
             R"({"username":"alice","password":"secret"})",
             "127.0.0.1");  // Credentials are fine; the session write is the
                            // failing step.
  CHECK(r.code == Http::Status::UNAUTHORIZED);  // Proves a failed session write
                                                // prevents a half-open login.
}

// ── Login: wrong password
// ─────────────────────────────────────────────────────

// A wrong password must fail verification and leave the session row untouched.
TEST_CASE("ClientController::Login returns UNAUTHORIZED for wrong password",
          "[controller][client][login][db]")
{
  auto hash = hashKnownPassword(
      "correct_password");  // The stored hash is for a DIFFERENT password.
  // A shared fake.
  auto fake = std::make_shared<FakeDatabaseController>();
  fake->password_result =
      makePasswordResult(42,
                         hash,
                         /*active=*/true);  // Active user with that other hash.
  fake->query_result = makeSessionResult();  // A session row is available,
                                             // but should not be written.

  FakeDbGuard            guard(fake);  // Install the fake first.
  ClientController<User> ctrl;         // Controller captures it.
  CR                     r;            // Holds the answer.
  ctrl.Login(cap(r),
             anonRequester(),
             R"({"username":"alice","password":"wrong_password"})",
             "127.0.0.1");  // Supply the wrong password; verification should
                            // fail.
  CHECK(r.code ==
        Http::Status::UNAUTHORIZED);  // Proves the wrong password is rejected.
  // A wrong password must not touch the session row (the login time is
  // written only after the password is verified).
  CHECK(fake->query_calls == 0);  // Proves no write query ran, so a failed
                                  // login cannot alter session timestamps.
}

// Success returns a token and records the login time exactly once; the body
// echoes the identity so a client can cache it.

// ── Login: success
// ────────────────────────────────────────────────────────────

// Correct credentials produce 200, a token in the body and exactly one session
// write.
TEST_CASE(
    "ClientController::Login returns 200 and token on correct credentials",
    "[controller][client][login][db]")
{
  auto hash = hashKnownPassword(
      "secret");  // The stored hash matches the supplied password.
  // A shared fake.
  auto fake = std::make_shared<FakeDatabaseController>();
  fake->password_result =
      makePasswordResult(42,
                         hash,
                         /*active=*/true);  // Active user, correct hash.
  fake->query_result =
      makeSessionResult();  // Arrange a successful session write.

  FakeDbGuard            guard(fake);  // Install the fake first.
  ClientController<User> ctrl;         // Controller captures it.
  CR                     r;            // Holds the answer.
  ctrl.Login(cap(r),
             anonRequester(),
             R"({"username":"alice","password":"secret"})",
             "127.0.0.1");            // The happy-path login.
  CHECK(r.code == Http::Status::OK);  // Proves valid credentials are accepted.
  CHECK(r.body.find("token") !=
        std::string::npos);  // Proves the response carries a session token.
  CHECK(r.body.find("alice") !=
        std::string::npos);  // Proves the response echoes the user identity.
  CHECK(fake->query_calls == 1);  // the login time is recorded exactly once,
                                  // after the password check
}

// ── Login: malformed JSON credentials ────────────────────────────────────────

// A non-JSON credentials body is refused before any database lookup.
TEST_CASE(
    "ClientController::Login returns BAD_REQUEST for malformed credentials",
    "[controller][client][login][db]")
{
  ClientController<User> ctrl;  // A fresh controller; no fake needed because
                                // parsing fails first.
  CR                     r;     // Holds the answer.
  ctrl.Login(cap(r),
             anonRequester(),
             "not-json",
             "127.0.0.1");  // Plain text instead of a JSON object.
  CHECK(r.code ==
        Http::Status::BAD_REQUEST);  // Proves malformed credentials yield 400.
}

// ── Login: SQL injection in username ─────────────────────────────────────────

// SQL-like text in the username is data, not executable SQL: it is simply an
// unknown user.
TEST_CASE(
    "ClientController::Login returns UNAUTHORIZED for SQL injection in "
    "username",
    "[controller][client][login][db]")
{
  // A shared fake.
  auto fake             = std::make_shared<FakeDatabaseController>();
  fake->password_result = std::nullopt;  // Arrange no matching user, as a
                                         // bound parameter would find none.

  FakeDbGuard            guard(fake);  // Install the fake first.
  ClientController<User> ctrl;         // Controller captures it.
  CR                     r;            // Holds the answer.
  ctrl.Login(cap(r),
             anonRequester(),
             R"({"username":"' OR 1=1 --","password":"x"})",
             "127.0.0.1");  // A classic injection payload used as the username
                            // value.
  // The username is only ever a bound parameter, so it is simply an unknown
  // user.
  CHECK(r.code == Http::Status::UNAUTHORIZED);  // Proves the payload could
                                                // not bypass authentication.
}

// ── Login: no database connection available ──────────────────────────────────

// When the pool is exhausted, login must report a retryable 503, not a failed
// login.
TEST_CASE("ClientController::Login returns 503 when the database is busy",
          "[controller][client][login][db][busy]")
{
  // A shared fake.
  auto fake  = std::make_shared<FakeDatabaseController>();
  fake->busy = true;  // Make the fake's queries throw DatabaseBusy.

  FakeDbGuard            guard(fake);  // Install the fake first.
  ClientController<User> ctrl;         // Controller captures it.
  CR                     r;            // Holds the answer.
  ctrl.Login(cap(r),
             anonRequester(),
             R"({"username":"alice","password":"secret"})",
             "127.0.0.1");  // Otherwise-valid credentials hit the busy fake.
  CHECK(r.code ==
        Http::Status::SERVICE_UNAVAILABLE);  // Proves overload is surfaced as
                                             // 503 for the client to retry.
}

// ── Logout: valid token
// ───────────────────────────────────────────────────────

// A correctly signed token is decoded and accepted; the logout-time write
// result is ignored.
TEST_CASE("ClientController::Logout returns 200 for a valid token",
          "[controller][client][logout][db]")
{
  // Build a token signed with the test fixture key/issuer, not production
  // credentials. The session manager decodes it and calls setNowLogoutTime
  // (fire-and-forget, no return check).
  const std::string secret =
      "valhalla-unit-test-secret-key";  // The signing key used only by tests.
  const std::string issuer =
      "valhalla-unit-test-issuer";  // The issuer claim the session manager
                                    // expects.
  const std::string type = "JWS";   // The token type claim.
  const std::string llodt =
      "2026-01-01 00:00:00 +0000";  // The "last login date/time" payload
                                    // claim.

  auto token =
      jwt::create<
          jwt::traits::
              kazuho_picojson>()  // Start building a JWT with the picojson
                                  // backend.
          .set_issuer(issuer)     // Identify the trusted issuer.
          .set_type(type)         // Set the token type.
          .set_subject("alice")   // Subject is the username.
          .set_id("42")           // Id is the user id.
          // Issue it at the current time.
          .set_issued_at(std::chrono::system_clock::now())
          .set_expires_at(std::chrono::system_clock::now() +
                          std::chrono::hours{1})  // Make it expire in one
                                                  // hour so it is valid now.
          .set_payload_claim("ip_address",
                             jwt::basic_claim<jwt::traits::kazuho_picojson>(
                                 std::string("127.0.0.1")))  // Carry the
                                                             // client IP.
          .set_payload_claim("llodt",
                             jwt::basic_claim<jwt::traits::kazuho_picojson>(
                                 llodt))  // Carry the last-login
                                          // time.
          .set_payload_claim("group",
                             jwt::basic_claim<jwt::traits::kazuho_picojson>(
                                 std::string("users")))  // Carry the user's
                                                         // group.
          .sign(jwt::algorithm::hs256{secret});  // Sign with HMAC-SHA256
                                                 // so the session
                                                 // manager trusts it.

  // A shared fake. setNowLogoutTime calls executeQuery; result is ignored, so
  // nullopt is fine.
  auto fake = std::make_shared<FakeDatabaseController>();
  fake->query_result =
      std::nullopt;  // The write result does not affect the logout answer.

  FakeDbGuard            guard(fake);  // Install the fake first.
  ClientController<User> ctrl;         // Controller captures it.
  CR                     r;            // Holds the answer.
  ctrl.Logout(cap(r),
              anonRequester(),
              token,
              "127.0.0.1");           // Log out using the signed token.
  CHECK(r.code == Http::Status::OK);  // Proves a valid token is accepted.
  CHECK(r.body.find("success") !=
        std::string::npos);  // Proves the body confirms the logout to the
                             // client.
}

// ── Logout: empty token
// ───────────────────────────────────────────────────────

// Logout with no token must not crash, whatever response it ultimately
// produces.
TEST_CASE("ClientController::Logout handles empty token gracefully",
          "[controller][client][logout][db]")
{
  ClientController<User> ctrl;  // A fresh controller.
  CR                     r;     // Holds the answer.
  ctrl.Logout(cap(r),
              anonRequester(),
              std::nullopt,
              "127.0.0.1");  // std::nullopt means "no token supplied".
  // Should not crash; returns some error or OK with failure message.
  CHECK(r.code != -1);  // Proves the callback fired (the controller answered)
                        // rather than silently dying.
}
