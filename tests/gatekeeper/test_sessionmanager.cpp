/**
 * @file test_sessionmanager.cpp
 * @brief Catch2 unit tests and fixtures for sessionmanager. Checks session
 * storage/lookup/removal, logout handling, and the login flow against a fake
 * database.
 * @details SessionManager keeps sessions in a process-wide cache and handles
 * login and logout. The fixture gives each instance a unique client id so cases
 * cannot see each other's sessions, and tokens are built with consistent claims
 * so decoding succeeds. The tests cover store/find/remove (including
 * idempotence and empty inputs), logout with bad and valid tokens, and
 * concurrent use. The login half injects a FakeDatabaseController to check
 * missing password hashes, wrong passwords, suspended accounts, successful
 * login, and exactly when the login time is written.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It protects sessionmanager.hpp, the component that decides whether a client
 * still has a valid session and performs the credential check at login. If
 * sessions leaked or were never invalidated on logout, revoked access would
 * persist; if login wrote state before verifying the password, failed attempts
 * would corrupt data. The fake-DB cases pin down the ordering and error
 * messages that clients depend on.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE / SECTION:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Catch2 CHECK / REQUIRE / REQUIRE_FALSE assertion macros:
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - RAII injection (FakeDbGuard restores the Store singleton on scope exit):
 * https://en.cppreference.com/w/cpp/language/raii
 * - std::optional, used for login data and DB results that may be absent:
 * https://en.cppreference.com/w/cpp/utility/optional
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// std::atomic counters and ids shared between worker threads.
#include <atomic>
// Provides TEST_CASE, SECTION and the CHECK/REQUIRE family of macros.
#include <catch2/catch_test_macros.hpp>
// Included for time-related helpers used by token construction.
#include <chrono>
#include <jsoncons/json.hpp>  // jsoncons::json, the shape of fake DB rows.
// std::optional / std::nullopt for login data and DB results.
#include <optional>
#include <string>  // std::string for tokens, messages and timestamps.
#include <thread>  // std::thread for the concurrent store-and-lookup test.
#include <vector>  // std::vector holds the worker threads.

// PasswordCrypt, used to make a real hash for login tests.
#include "gatekeeper/passwordcrypt/passwordcrypt.hpp"
// The production header under test: SessionManager.
#include "gatekeeper/sessionmanager/sessionmanager.hpp"
// Shared gatekeeper types, including ClientLoginData.
#include "gatekeeper/types.hpp"
// makeLoginData / makeToken test-data builders.
#include "support/builders.hpp"
#include "support/fake_db.hpp"  // FakeDatabaseController and FakeDbGuard.
// api::v2::Http::Status codes used by logout assertions.
#include "utils/global/http.hpp"

using SessionManager =
    api::v2::SessionManager;  // Short alias for the class under test.
using ClientLoginData =
    api::v2::Types::ClientLoginData;  // Short alias for the login-data type.
using valhalla_test::makeLoginData;   // Import the builder so tests can call it
                                      // unqualified.
using valhalla_test::makeToken;       // Import the token builder for the same
                                      // reason.

namespace {
  // Test token parameters - must match tests/support/global_env.cpp.
  constexpr const char* kSecret =
      "valhalla-unit-test-secret-key";  // Signing secret, matching the token
                                        // manager's configured value.
  constexpr const char* kIssuer =
      "valhalla-unit-test-issuer";      // Issuer expected when decoding test
                                        // tokens.
  constexpr const char* kType = "JWS";  // Token type string used when signing.

  /**
   * A session manager plus a login data object whose token/claims are
   * consistent, so decodeToken() (claim extraction) succeeds.
   *
   * Each fixture instance gets a UNIQUE client id: SessionManager's
   * session cache (MemCache) is a process-wide singleton, so tests that
   * share a key would observe each other's sessions.
   */
  struct SessionFixture {
    SessionManager
        manager;  // The class under test, with its process-wide cache.
    std::optional<ClientLoginData>
        loginData;  // Consistent login data whose claims match its token.

    // Builds data for a given group
    // and IP, with sensible defaults.
    SessionFixture(const std::string& group = "users",
                   const std::string& ip    = "127.0.0.1") {
      // Unique per fixture instance, stable for the lifetime of the run.
      static std::atomic<uint64_t> nextClientId{
          1000};  // Static counter shared by all fixtures; atomic so
                  // parallel construction is safe.
      const auto clientId =
          nextClientId.fetch_add(1);  // Take a fresh id and increment for next
                                      // time, guaranteeing uniqueness.

      loginData =
          makeLoginData(clientId,
                        group,
                        ip);  // Build the base login data with that unique id.
      loginData->username =
          "sessionuser";  // Set the subject used both in data and token.
      loginData->lastLogoutTime =
          "2026-01-02 03:04:05 +0000";  // Fixed last-logout time,
                                        // embedded in the token too.
      loginData->token =
          makeToken(kSecret,
                    kIssuer,
                    kType,
                    "sessionuser",
                    clientId,
                    group,
                    ip,
                    *loginData->lastLogoutTime);  // Sign a token whose claims
                                                  // agree with the data, so
                                                  // decoding succeeds.
    }
  };
}  // namespace

// Registers the basic store-then-lookup test.
TEST_CASE("SessionManager stores and finds a session",
          "[gatekeeper][session]") {
  SessionFixture fixture;  // Fresh manager and unique login data.

  std::string message;  // Receives any status message.
  REQUIRE(fixture.manager
              .storeSession(fixture.loginData,
                            message));  // Prove the session can be stored.
  CHECK(message.empty());  // Prove no error message was produced on success.
  CHECK(fixture.manager
            .clientHasValidSession(fixture.loginData,
                                   message));  // Prove the stored session is
                                               // found and considered valid.
}

// Registers the test for a client with no
// stored session.
TEST_CASE("SessionManager reports an unknown session as invalid",
          "[gatekeeper][session]") {
  SessionFixture fixture;  // never stored -- the fixture only builds data; it
                           // does not call storeSession.

  std::string message;  // Receives any status message.
  REQUIRE_FALSE(
      fixture.manager
          .clientHasValidSession(fixture.loginData,
                                 message));  // Prove an unstored client is
                                             // reported as having no valid
                                             // session.
}

// Registers the test that removal
// invalidates a session.
TEST_CASE("SessionManager invalidates a session on logout (remove)",
          "[gatekeeper][session]") {
  SessionFixture fixture;  // Fresh manager and data.

  std::string message;  // Receives status messages.
  REQUIRE(fixture.manager.storeSession(fixture.loginData,
                                       message));  // Store the session first.
  REQUIRE(
      fixture.manager.clientHasValidSession(fixture.loginData,
                                            message));  // Confirm it is valid
                                                        // before removal.

  REQUIRE(fixture.manager.removeSession(
      fixture.loginData));  // Remove it (the logout path).
  REQUIRE_FALSE(
      fixture.manager
          .clientHasValidSession(fixture.loginData,
                                 message));  // Prove the session is no longer
                                             // valid after removal.
}

// Registers the test that removing a
// missing session is still safe.
TEST_CASE("SessionManager removeSession is idempotent",
          "[gatekeeper][session]") {
  SessionFixture fixture;  // No session was ever stored for this unique id.

  REQUIRE(fixture.manager.removeSession(
      fixture.loginData));  // First removal succeeds even though nothing was
                            // stored.
  REQUIRE(fixture.manager.removeSession(
      fixture.loginData));  // Second removal also succeeds, proving the
                            // operation is idempotent.
}

// Registers the defensive tests for
// malformed arguments.
TEST_CASE("SessionManager rejects empty inputs", "[gatekeeper][session]") {
  SessionFixture fixture;  // Fresh manager and data.
  std::string    message;  // Receives status messages.

  SECTION("storeSession with empty login data") {
    std::optional<ClientLoginData> empty;  // No login data supplied.
    REQUIRE_FALSE(
        fixture.manager
            .storeSession(empty,
                          message));  // Prove storing nothing is refused.
    CHECK(message ==
          "clientLoginData is empty");  // Prove the caller gets a specific,
                                        // useful error message.
  }

  SECTION("removeSession with empty login data") {
    std::optional<ClientLoginData> empty;  // No login data supplied.
    REQUIRE_FALSE(fixture.manager.removeSession(
        empty));  // Prove removal without data is refused.
  }

  SECTION("removeSession without a client id") {
    std::optional<ClientLoginData> noId =
        makeLoginData();     // Data exists but lacks the key needed to find a
                             // session.
    noId->clientId.reset();  // Explicitly remove the client id.
    REQUIRE_FALSE(fixture.manager.removeSession(
        noId));              // Prove removal is refused without an id.
    CHECK(message.empty());  // error goes to a local string; only the
                             // result is observable -- this overload has no
                             // message argument.
  }
}

// Registers the tests for logout failures
// that are detected before any DB access.
TEST_CASE("SessionManager logout with a bad token answers without a database",
          "[gatekeeper][session]") {
  SessionFixture fixture;  // Fresh manager and data.

  SECTION("unparseable token") {
    fixture.loginData->token = "not a jwt";  // Replace the valid token with
                                             // text that cannot be decoded.
    int statusCode = -1;  // Receives the HTTP status from the callback; -1
                          // means "not called".
    std::string body;     // Receives the response body.
    fixture.manager.logout(
        [&statusCode, &body](const int code, const std::string& response) {
          statusCode = code;      // The callback records the status...
          body       = response;  // ...and the body.
        },
        fixture.loginData);  // The callback is how logout returns its
                             // answer asynchronously.
    // No valid session for a garbage token.
    CHECK(statusCode ==
          api::v2::Http::Status::UNAUTHORIZED);  // Prove a garbage token is
                                                 // rejected as 401.
    CHECK_FALSE(body.empty());  // Prove some explanatory message was returned.
  }

  SECTION("well-formed token, wrong ip address") {
    // The stored token was issued for 127.0.0.1; claim a different ip.
    fixture.loginData->ip_address =
        "8.8.8.8";                // Mismatch the IP bound into the token.
    int         statusCode = -1;  // Receives the status.
    std::string body;             // Receives the response body.
    fixture.manager.logout(
        [&statusCode, &body](const int code, const std::string& response) {
          statusCode = code;      // Record the status...
          body       = response;  // ...and the body.
        },
        fixture.loginData);
    CHECK(statusCode ==
          api::v2::Http::Status::UNAUTHORIZED);  // Prove the IP mismatch is
                                                 // rejected as 401.
    CHECK(body.find("ip address") !=
          std::string::npos);  // Prove the message names the IP as the
                               // problem.
  }
}

// Registers the positive logout test.
TEST_CASE("SessionManager logout with a valid token removes the session",
          "[gatekeeper][session]") {
  SessionFixture fixture;  // Fresh manager and data.
  std::string    message;  // Receives status messages.
  REQUIRE(
      fixture.manager.storeSession(fixture.loginData,
                                   message));  // Store a session so there is
                                               // something to log out of.

  int         statusCode = -1;  // Receives the status from the callback.
  std::string body;             // Receives the response body.
  fixture.manager.logout(
      [&statusCode, &body](const int code, const std::string& response) {
        statusCode = code;      // Record the status...
        body       = response;  // ...and the body.
      },
      fixture.loginData);

  CHECK(statusCode ==
        api::v2::Http::Status::OK);  // Prove a valid logout returns 200.
  CHECK(body.find("Logout success") !=
        std::string::npos);  // Prove the body confirms the logout.
  // The session is gone.
  CHECK_FALSE(
      fixture.manager
          .clientHasValidSession(fixture.loginData,
                                 message));  // Prove the session was actually
                                             // removed, not just reported as
                                             // logged out.
}

// Registers the multithreaded test
// for the shared session cache.
TEST_CASE("SessionManager handles concurrent store and lookup",
          "[gatekeeper][session][concurrency]") {
  SessionFixture fixture;  // Fresh manager; its own id is separate from the
                           // ones used below.
  std::string    message;  // Shared message string; only truth of operations is
                           // checked, not its text.

  constexpr int kThreads = 4;  // Number of concurrent workers.
  constexpr int kPerThread =
      25;  // Sessions each worker creates, for 100 total.
  std::atomic<int> storedFailures{0};  // Counts failed stores across threads.
  std::atomic<int> lookupFailures{0};  // Counts failed lookups across threads.
  std::vector<std::thread> workers;    // Holds the workers for joining later.

  // Start one worker per thread.
  for (int t = 0; t < kThreads; ++t) {
    workers.emplace_back(
        [&fixture, &storedFailures, &lookupFailures, &message, t]() {
          for (int i = 0; i < kPerThread;
               // Each worker creates and checks its own sessions.
               ++i) {
            // A distinct high id range: the session cache is a
            // process-wide singleton, so these must not collide with
            // the fixture's key (1000+) or another test's sessions.
            std::optional<ClientLoginData> data = makeLoginData(
                100000 + t * kPerThread + i);  // Ids start at 100000, well
                                               // clear of the fixture's range.
            data->username = "user";           // Subject for the token.
            data->group    = "users";  // Group for the token and cache key.
            data->lastLogoutTime = "llodt";  // Last-logout claim.
            data->token =
                makeToken(kSecret,
                          kIssuer,
                          kType,
                          "user",
                          data->clientId.value(),
                          "users",
                          "127.0.0.1",
                          "llodt");  // Sign a token with matching claims.

            // Try to store this
            // worker's session.
            if (!fixture.manager.storeSession(data, message)) {
              ++storedFailures;  // Record a failure atomically.
            }
            // Immediately try to
            // find it again.
            if (!fixture.manager.clientHasValidSession(data, message)) {
              ++lookupFailures;  // Record a failure atomically.
            }
          }
        });
  }
  // Iterate over all workers.
  for (auto& worker : workers) {
    worker.join();  // Wait for each to finish before checking the counters.
  }

  CHECK(storedFailures.load() == 0);  // Prove every concurrent store succeeded.
  CHECK(lookupFailures.load() ==
        0);  // Prove every stored session was immediately findable.
}

// ── SessionManager::login tests (via FakeDatabaseController) ─────────────────
// The FakeDbGuard is constructed before the SessionManager in every case below
// so the manager binds the fake controller from the Store at construction time.

namespace {
  /**
   * JSON row that getPasswordHashForUserName() returns on success.
   */
  // Builds the fake DB row returned when looking up a
  // password.
  jsoncons::json makePasswordRow(uint64_t           id,
                                 const std::string& hash,
                                 bool               active) {
    jsoncons::json row =
        jsoncons::json::object();  // Start with an empty JSON object.
    row["id"]       = id;      // Client id, used to populate the login data.
    row["password"] = hash;    // Stored password hash used for verification.
    row["active"]   = active;  // Whether the account is enabled.
    return row;                // Return the complete row.
  }

  /**
   * JSON row that setNowLoginTimeGetLastLogoutTime() reads from
   * executeQuery().
   */
  // Builds the row returned when
  // writing/reading the login time.
  jsoncons::json makeLoginTimeRow(const std::string& lastLogout) {
    jsoncons::json row =
        jsoncons::json::object();  // Start with an empty JSON object.
    row["last_logout"] =
        lastLogout;  // The last-logout timestamp the helper will read.
    return row;      // Return the row.
  }
}  // namespace

// Registers the test for an unknown
// username.
TEST_CASE(
    "SessionManager::login returns false when DB returns no password hash",
    "[gatekeeper][session][login]") {
  auto fake =
      std::make_shared<FakeDatabaseController>();  // Create the fake DB.
  FakeDbGuard guard(fake);  // Inject it BEFORE the SessionManager is
                            // constructed, so the manager binds to it.

  fake->password_result = std::nullopt;  // Simulate "no user with this name"
                                         // from the password lookup.

  SessionManager sm;  // Constructed after the guard, so it uses the fake.
  std::optional<ClientLoginData> cld =
      makeLoginData(2000,
                    "users",
                    "127.0.0.1");  // Login data to be filled by a successful
                                   // login.
  api::v2::Types::Credentials
      creds{"testuser",
            "somepassword"};  // The username/password being attempted.

  std::string message;  // Receives the error text.
  CHECK_FALSE(
      sm.login(std::optional{creds},
               cld,
               message));  // Prove login fails when no password row exists.
  CHECK(message.find("does not exist") !=
        std::string::npos);  // Prove the message tells the caller the user
                             // does not exist.
}

// Registers the test that a bad password
// is rejected without side effects.
TEST_CASE("SessionManager::login returns false for a wrong password",
          "[gatekeeper][session][login]") {
  auto fake =
      std::make_shared<FakeDatabaseController>();  // Create the fake DB.
  FakeDbGuard guard(fake);  // Inject it before the manager is built.

  // Hash for a different password ("correct_password").
  PasswordCrypt pc;  // The hasher used to make a real hash.
  auto          hash_opt = pc.hashPassword(
      "correct_password");        // Hash the password that WOULD be correct.
  REQUIRE(hash_opt.has_value());  // Prove hashing succeeded.
  const std::string correct_hash = hash_opt.value();  // Keep that hash.

  fake->password_result =
      makePasswordRow(2001,
                      correct_hash,
                      true);  // DB returns a row for an active account with
                              // that hash.
  // setNowLoginTimeGetLastLogoutTime() calls executeQuery(); supply a row.
  fake->query_result = makeLoginTimeRow(
      "2026-01-01 00:00:00 +0000");  // Provide a row in case the code tries
                                     // the login-time write.

  SessionManager                 sm;  // Manager bound to the fake.
  std::optional<ClientLoginData> cld =
      makeLoginData(2001,
                    "users",
                    "127.0.0.1");  // Login data for the same client id as the
                                   // row.
  api::v2::Types::Credentials
      creds{"testuser",
            "wrong_password"};  // Deliberately supply a different password.

  std::string message;  // Receives the error text.
  CHECK_FALSE(sm.login(std::optional{creds},
                       cld,
                       message));  // Prove the wrong password is rejected.
  CHECK(message.find("Invalid") !=
        std::string::npos);  // Prove the reason is reported as an invalid
                             // credential.
  CHECK(fake->query_calls ==
        0);  // the login time is not written for a wrong password -- proves
             // verification happens before any write
}

// Registers the positive login test.
TEST_CASE(
    "SessionManager::login succeeds with correct password and active account",
    "[gatekeeper][session][login]") {
  auto fake =
      std::make_shared<FakeDatabaseController>();  // Create the fake DB.
  FakeDbGuard guard(fake);  // Inject it before the manager is built.

  PasswordCrypt     pc;  // The hasher.
  const std::string password =
      "GoodPass123!";  // The correct password for this test.
  auto hash_opt = pc.hashPassword(password);  // Hash it.
  REQUIRE(hash_opt.has_value());              // Prove hashing succeeded.
  const std::string correct_hash = hash_opt.value();  // Keep the hash.

  fake->password_result = makePasswordRow(2002,
                                          correct_hash,
                                          true);  // DB returns the matching
                                                  // hash for an active account.
  fake->query_result    = makeLoginTimeRow(
      "2026-01-01 00:00:00 +0000");  // Supply the login-time row the final
                                     // write will read.

  SessionManager                 sm;  // Manager bound to the fake.
  std::optional<ClientLoginData> cld =
      makeLoginData(2002,
                    "users",
                    "127.0.0.1");  // Login data for the matching client id.
  api::v2::Types::Credentials creds{"testuser",
                                    password};  // Correct credentials.

  std::string message;  // Receives any error text.
  CHECK(sm.login(std::optional{creds},
                 cld,
                 message));  // Prove a correct, active login succeeds.
}

// Registers the test that the
// password-lookup step is read-only.
TEST_CASE("SessionManager::fetchPasswordHash reads without writing",
          "[gatekeeper][session][login]") {
  auto fake =
      std::make_shared<FakeDatabaseController>();  // Create the fake DB.
  FakeDbGuard guard(fake);  // Inject it before the manager is built.

  fake->password_result =
      makePasswordRow(2003,
                      "stored-hash",
                      true);  // DB returns a fixed hash for an active account.

  SessionManager                 sm;  // Manager bound to the fake.
  std::optional<ClientLoginData> cld =
      makeLoginData(2003, "users", "127.0.0.1");  // Login data to be populated.
  api::v2::Types::Credentials
      creds{"testuser", "whatever"};  // Credentials whose password is
                                      // irrelevant for this read-only step.

  std::string message;  // Receives any error text.
  const auto  hash =
      sm.fetchPasswordHash(std::optional{creds},
                           cld,
                           message);  // Ask only for the stored hash.
  REQUIRE(hash.has_value());  // Prove a hash was returned before reading it.
  CHECK(*hash ==
        "stored-hash");  // Prove it is the exact string the fake supplied.
  CHECK(cld->clientId == 2003);  // Prove the lookup also filled in the client
                                 // id from the DB row.
  CHECK(fake->query_calls ==
        0);  // Prove no write-capable query ran during this read-only step.
}

// Registers the test that disabled
// accounts cannot even fetch their hash.
TEST_CASE("SessionManager::fetchPasswordHash refuses a suspended account",
          "[gatekeeper][session][login]") {
  auto fake =
      std::make_shared<FakeDatabaseController>();  // Create the fake DB.
  FakeDbGuard guard(fake);  // Inject it before the manager is built.

  fake->password_result =
      makePasswordRow(2004,
                      "stored-hash",
                      false);  // Same hash but active=false.

  SessionManager                 sm;  // Manager bound to the fake.
  std::optional<ClientLoginData> cld =
      makeLoginData(2004,
                    "users",
                    "127.0.0.1");  // Login data for the matching client.
  // Credentials.
  api::v2::Types::Credentials creds{"testuser", "whatever"};

  std::string message;  // Receives the error text.
  CHECK_FALSE(sm.fetchPasswordHash(std::optional{creds}, cld, message)
                  .has_value());  // Prove no hash is returned for a suspended
                                  // account.
  CHECK(message.find("suspended") !=
        std::string::npos);  // Prove the reason explicitly mentions suspension.
}

TEST_CASE(
    "SessionManager::completeLogin verifies first and writes the login time "
    // Registers the test for the final login
    // step and its write ordering.
    "once",
    "[gatekeeper][session][login]") {
  auto fake =
      std::make_shared<FakeDatabaseController>();  // Create the fake DB.
  FakeDbGuard guard(fake);  // Inject it before the manager is built.

  PasswordCrypt pc;  // The hasher.
  auto          hash_opt = pc.hashPassword(
      "GoodPass123!");            // Hash the correct password for this test.
  REQUIRE(hash_opt.has_value());  // Prove hashing succeeded.

  fake->query_result = makeLoginTimeRow(
      "2026-01-01 00:00:00 +0000");  // Supply the row the login-time write
                                     // will read.

  SessionManager                 sm;  // Manager bound to the fake.
  std::optional<ClientLoginData> cld =
      makeLoginData(2005, "users", "127.0.0.1");  // Login data to complete.
  std::string message;                            // Receives status messages.

  CHECK_FALSE(
      sm.completeLogin(std::optional{api::v2::Types::Credentials{"u", "bad"}},
                       *hash_opt,
                       cld,
                       message));  // A wrong password must fail verification.
  CHECK(fake->query_calls ==
        0);  // Prove the failing attempt did NOT write the login time.

  CHECK(sm.completeLogin(std::optional{api::v2::Types::
                                           Credentials{"u", "GoodPass123!"}},
                         *hash_opt,
                         cld,
                         message));  // The correct password succeeds.
  CHECK(fake->query_calls == 1);     // Prove exactly one write happened, only
                                     // after successful verification.
}
