#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <jsoncons/json.hpp>

#include "gatekeeper/passwordcrypt/passwordcrypt.hpp"
#include "gatekeeper/sessionmanager/sessionmanager.hpp"
#include "gatekeeper/types.hpp"
#include "support/builders.hpp"
#include "support/fake_db.hpp"
#include "utils/global/http.hpp"

using SessionManager = api::v2::SessionManager;
using ClientLoginData = api::v2::Types::ClientLoginData;
using valhalla_test::makeLoginData;
using valhalla_test::makeToken;

namespace
{
    // Test token parameters - must match tests/support/global_env.cpp.
    constexpr const char *kSecret = "valhalla-unit-test-secret-key";
    constexpr const char *kIssuer = "valhalla-unit-test-issuer";
    constexpr const char *kType   = "JWS";

    /// A session manager plus a login data object whose token/claims are
    /// consistent, so decodeToken() (claim extraction) succeeds.
    ///
    /// Each fixture instance gets a UNIQUE client id: SessionManager's
    /// session cache (MemCache) is a process-wide singleton, so tests that
    /// share a key would observe each other's sessions.
    struct SessionFixture
    {
        SessionManager                 manager;
        std::optional<ClientLoginData> loginData;

        SessionFixture(const std::string &group = "users", const std::string &ip = "127.0.0.1")
        {
            // Unique per fixture instance, stable for the lifetime of the run.
            static std::atomic<uint64_t> nextClientId{1000};
            const auto                   clientId = nextClientId.fetch_add(1);

            loginData                 = makeLoginData(clientId, group, ip);
            loginData->username       = "sessionuser";
            loginData->lastLogoutTime = "2026-01-02 03:04:05 +0000";
            loginData->token          = makeToken(kSecret, kIssuer, kType, "sessionuser", clientId, group, ip, *loginData->lastLogoutTime);
        }
    };
}  // namespace

TEST_CASE("SessionManager stores and finds a session", "[gatekeeper][session]")
{
    SessionFixture fixture;

    std::string message;
    REQUIRE(fixture.manager.storeSession(fixture.loginData, message));
    CHECK(message.empty());
    CHECK(fixture.manager.clientHasValidSession(fixture.loginData, message));
}

TEST_CASE("SessionManager reports an unknown session as invalid", "[gatekeeper][session]")
{
    SessionFixture fixture;  // never stored

    std::string message;
    REQUIRE_FALSE(fixture.manager.clientHasValidSession(fixture.loginData, message));
}

TEST_CASE("SessionManager invalidates a session on logout (remove)", "[gatekeeper][session]")
{
    SessionFixture fixture;

    std::string message;
    REQUIRE(fixture.manager.storeSession(fixture.loginData, message));
    REQUIRE(fixture.manager.clientHasValidSession(fixture.loginData, message));

    REQUIRE(fixture.manager.removeSession(fixture.loginData));
    REQUIRE_FALSE(fixture.manager.clientHasValidSession(fixture.loginData, message));
}

TEST_CASE("SessionManager removeSession is idempotent", "[gatekeeper][session]")
{
    SessionFixture fixture;

    REQUIRE(fixture.manager.removeSession(fixture.loginData));
    REQUIRE(fixture.manager.removeSession(fixture.loginData));
}

TEST_CASE("SessionManager rejects empty inputs", "[gatekeeper][session]")
{
    SessionFixture fixture;
    std::string     message;

    SECTION("storeSession with empty login data")
    {
        std::optional<ClientLoginData> empty;
        REQUIRE_FALSE(fixture.manager.storeSession(empty, message));
        CHECK(message == "clientLoginData is empty");
    }

    SECTION("removeSession with empty login data")
    {
        std::optional<ClientLoginData> empty;
        REQUIRE_FALSE(fixture.manager.removeSession(empty));
    }

    SECTION("removeSession without a client id")
    {
        std::optional<ClientLoginData> noId = makeLoginData();
        noId->clientId.reset();
        REQUIRE_FALSE(fixture.manager.removeSession(noId));
        CHECK(message.empty());  // error goes to a local string; only the result is observable
    }
}

TEST_CASE("SessionManager logout with a bad token answers without a database", "[gatekeeper][session]")
{
    SessionFixture fixture;

    SECTION("unparseable token")
    {
        fixture.loginData->token = "not a jwt";
        int         statusCode   = -1;
        std::string body;
        fixture.manager.logout(
            [&statusCode, &body](const int code, const std::string &response)
            {
                statusCode = code;
                body       = response;
            },
            fixture.loginData);
        // jwt::decode throws for garbage input -> internal server error.
        CHECK(statusCode == api::v2::Http::Status::INTERNAL_SERVER_ERROR);
        CHECK_FALSE(body.empty());
    }

    SECTION("well-formed token, wrong ip address")
    {
        // The stored token was issued for 127.0.0.1; claim a different ip.
        fixture.loginData->ip_address = "8.8.8.8";
        int         statusCode        = -1;
        std::string body;
        fixture.manager.logout(
            [&statusCode, &body](const int code, const std::string &response)
            {
                statusCode = code;
                body       = response;
            },
            fixture.loginData);
        CHECK(statusCode == api::v2::Http::Status::OK);
        CHECK(body.find("ip address") != std::string::npos);
    }
}

TEST_CASE("SessionManager logout with a valid token removes the session", "[gatekeeper][session]")
{
    SessionFixture fixture;
    std::string     message;
    REQUIRE(fixture.manager.storeSession(fixture.loginData, message));

    int         statusCode = -1;
    std::string body;
    fixture.manager.logout(
        [&statusCode, &body](const int code, const std::string &response)
        {
            statusCode = code;
            body       = response;
        },
        fixture.loginData);

    CHECK(statusCode == api::v2::Http::Status::OK);
    CHECK(body.find("Logout success") != std::string::npos);
    // The session is gone.
    CHECK_FALSE(fixture.manager.clientHasValidSession(fixture.loginData, message));
}

TEST_CASE("SessionManager handles concurrent store and lookup", "[gatekeeper][session][concurrency]")
{
    SessionFixture fixture;
    std::string     message;

    constexpr int            kThreads = 4;
    constexpr int            kPerThread = 25;
    std::atomic<int>         storedFailures{0};
    std::atomic<int>         lookupFailures{0};
    std::vector<std::thread> workers;

    for (int t = 0; t < kThreads; ++t)
    {
        workers.emplace_back(
            [&fixture, &storedFailures, &lookupFailures, &message, t]()
            {
                for (int i = 0; i < kPerThread; ++i)
                {
                    std::optional<ClientLoginData> data = makeLoginData(100000 + t * kPerThread + i);
                    data->username                      = "user";
                    data->group                         = "users";
                    data->lastLogoutTime                = "llodt";
                    data->token = makeToken(kSecret, kIssuer, kType, "user", data->clientId.value(), "users", "127.0.0.1", "llodt");

                    if (!fixture.manager.storeSession(data, message))
                    {
                        ++storedFailures;
                    }
                    if (!fixture.manager.clientHasValidSession(data, message))
                    {
                        ++lookupFailures;
                    }
                }
            });
    }
    for (auto &worker : workers)
    {
        worker.join();
    }

    CHECK(storedFailures.load() == 0);
    CHECK(lookupFailures.load() == 0);
}

// ── SessionManager::login tests (via FakeDatabaseController) ─────────────────

namespace
{
    /// JSON row that getPasswordHashForUserName() returns on success.
    jsoncons::json makePasswordRow(uint64_t id, const std::string &hash, bool active)
    {
        jsoncons::json row = jsoncons::json::object();
        row["id"]          = id;
        row["password"]    = hash;
        row["active"]      = active;
        return row;
    }

    /// JSON row that setNowLoginTimeGetLastLogoutTime() reads from executeQuery().
    jsoncons::json makeLoginTimeRow(const std::string &lastLogout)
    {
        jsoncons::json row  = jsoncons::json::object();
        row["last_logout"]  = lastLogout;
        return row;
    }
}  // namespace

TEST_CASE("SessionManager::login returns false when DB returns no password hash", "[gatekeeper][session][login]")
{
    auto fake = std::make_shared<FakeDatabaseController>();
    FakeDbGuard guard(fake);

    fake->password_result = std::nullopt;

    SessionManager              sm;
    std::optional<ClientLoginData> cld = makeLoginData(2000, "users", "127.0.0.1");
    api::v2::Types::Credentials    creds{"testuser", "somepassword"};

    std::string message;
    CHECK_FALSE(sm.login(std::optional{creds}, cld, message));
    CHECK(message.find("does not exist") != std::string::npos);
}

TEST_CASE("SessionManager::login returns false for a wrong password", "[gatekeeper][session][login]")
{
    auto fake = std::make_shared<FakeDatabaseController>();
    FakeDbGuard guard(fake);

    // Hash for a different password ("correct_password").
    PasswordCrypt     pc;
    auto              hash_opt     = pc.hashPassword("correct_password");
    REQUIRE(hash_opt.has_value());
    const std::string correct_hash = hash_opt.value();

    fake->password_result = makePasswordRow(2001, correct_hash, true);
    // setNowLoginTimeGetLastLogoutTime() calls executeQuery(); supply a row.
    fake->query_result = makeLoginTimeRow("2026-01-01 00:00:00 +0000");

    SessionManager              sm;
    std::optional<ClientLoginData> cld = makeLoginData(2001, "users", "127.0.0.1");
    api::v2::Types::Credentials    creds{"testuser", "wrong_password"};

    std::string message;
    CHECK_FALSE(sm.login(std::optional{creds}, cld, message));
    CHECK(message.find("Invalid") != std::string::npos);
}

TEST_CASE("SessionManager::login succeeds with correct password and active account", "[gatekeeper][session][login]")
{
    auto fake = std::make_shared<FakeDatabaseController>();
    FakeDbGuard guard(fake);

    PasswordCrypt     pc;
    const std::string password      = "GoodPass123!";
    auto              hash_opt      = pc.hashPassword(password);
    REQUIRE(hash_opt.has_value());
    const std::string correct_hash  = hash_opt.value();

    fake->password_result = makePasswordRow(2002, correct_hash, true);
    fake->query_result    = makeLoginTimeRow("2026-01-01 00:00:00 +0000");

    SessionManager              sm;
    std::optional<ClientLoginData> cld = makeLoginData(2002, "users", "127.0.0.1");
    api::v2::Types::Credentials    creds{"testuser", password};

    std::string message;
    CHECK(sm.login(std::optional{creds}, cld, message));
}
