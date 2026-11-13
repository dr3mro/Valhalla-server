// DB-path tests for Login and Logout via ClientController<User>.
// Uses FakeDatabaseController to simulate DB responses without PostgreSQL.
#include <catch2/catch_test_macros.hpp>
#include <jsoncons/json.hpp>
#include <memory>
#include <string>

#include "controllers/clientcontroller/clientcontroller.hpp"
#include "entities/people/user.hpp"
#include "gatekeeper/passwordcrypt/passwordcrypt.hpp"
#include "support/fake_db.hpp"
#include "utils/global/callback.hpp"
#include "utils/global/http.hpp"
#include "utils/global/requester.hpp"

namespace Http = api::v2::Http;
using api::v2::Requester;

namespace
{
    struct CR
    {
        int         code = -1;
        std::string body;
    };
    CALLBACK_ cap(CR& r)
    {
        return [&r](int c, const std::string& b)
        {
            r.code = c;
            r.body = b;
        };
    }

    // The anon requester passed to Login/Logout (requester is unused in those methods).
    Requester anonRequester() { return Requester(0, "users"); }

    // Build password_result JSON: {id, password: <hash>, active: true}.
    jsoncons::json makePasswordResult(uint64_t id, const std::string& hash, bool active = true)
    {
        jsoncons::json j = jsoncons::json::object();
        j["id"]          = id;
        j["password"]    = hash;
        j["active"]      = active;
        return j;
    }

    // Build query_result JSON for setNowLoginTimeGetLastLogoutTime (RETURNING last_logout).
    jsoncons::json makeSessionResult(const std::string& lastLogout = "2026-01-01 00:00:00 +0000")
    {
        jsoncons::json j = jsoncons::json::object();
        j["last_logout"] = lastLogout;
        return j;
    }

    // Hash a known password via PasswordCrypt (sodium, stable across the process).
    std::string hashKnownPassword(const std::string& pw)
    {
        PasswordCrypt crypt;
        auto          h = crypt.hashPassword(pw);
        REQUIRE(h.has_value());
        return h.value();
    }
}  // namespace

// ── Login: DB returns nullopt (user not found) ───────────────────────────────

TEST_CASE("ClientController::Login returns UNAUTHORIZED when user not in DB", "[controller][client][login][db]")
{
    auto fake             = std::make_shared<FakeDatabaseController>();
    fake->password_result = std::nullopt;  // user does not exist

    FakeDbGuard            guard(fake);
    ClientController<User> ctrl;
    CR                     r;
    ctrl.Login(cap(r), anonRequester(), R"({"username":"alice","password":"secret"})", "127.0.0.1");
    CHECK(r.code == Http::Status::UNAUTHORIZED);
}

// ── Login: user is suspended ─────────────────────────────────────────────────

TEST_CASE("ClientController::Login returns UNAUTHORIZED when user is suspended", "[controller][client][login][db]")
{
    auto hash             = hashKnownPassword("secret");
    auto fake             = std::make_shared<FakeDatabaseController>();
    fake->password_result = makePasswordResult(42, hash, /*active=*/false);

    FakeDbGuard            guard(fake);
    ClientController<User> ctrl;
    CR                     r;
    ctrl.Login(cap(r), anonRequester(), R"({"username":"alice","password":"secret"})", "127.0.0.1");
    CHECK(r.code == Http::Status::UNAUTHORIZED);
    CHECK(r.body.find("suspended") != std::string::npos);
}

// ── Login: session INSERT fails ───────────────────────────────────────────────

TEST_CASE("ClientController::Login returns UNAUTHORIZED when session INSERT fails", "[controller][client][login][db]")
{
    auto hash             = hashKnownPassword("secret");
    auto fake             = std::make_shared<FakeDatabaseController>();
    fake->password_result = makePasswordResult(42, hash, /*active=*/true);
    fake->query_result    = std::nullopt;  // setNowLoginTimeGetLastLogoutTime fails

    FakeDbGuard            guard(fake);
    ClientController<User> ctrl;
    CR                     r;
    ctrl.Login(cap(r), anonRequester(), R"({"username":"alice","password":"secret"})", "127.0.0.1");
    CHECK(r.code == Http::Status::UNAUTHORIZED);
}

// ── Login: wrong password ─────────────────────────────────────────────────────

TEST_CASE("ClientController::Login returns UNAUTHORIZED for wrong password", "[controller][client][login][db]")
{
    auto hash             = hashKnownPassword("correct_password");
    auto fake             = std::make_shared<FakeDatabaseController>();
    fake->password_result = makePasswordResult(42, hash, /*active=*/true);
    fake->query_result    = makeSessionResult();

    FakeDbGuard            guard(fake);
    ClientController<User> ctrl;
    CR                     r;
    ctrl.Login(cap(r), anonRequester(), R"({"username":"alice","password":"wrong_password"})", "127.0.0.1");
    CHECK(r.code == Http::Status::UNAUTHORIZED);
}

// ── Login: success ────────────────────────────────────────────────────────────

TEST_CASE("ClientController::Login returns 200 and token on correct credentials", "[controller][client][login][db]")
{
    auto hash             = hashKnownPassword("secret");
    auto fake             = std::make_shared<FakeDatabaseController>();
    fake->password_result = makePasswordResult(42, hash, /*active=*/true);
    fake->query_result    = makeSessionResult();

    FakeDbGuard            guard(fake);
    ClientController<User> ctrl;
    CR                     r;
    ctrl.Login(cap(r), anonRequester(), R"({"username":"alice","password":"secret"})", "127.0.0.1");
    CHECK(r.code == Http::Status::OK);
    CHECK(r.body.find("token") != std::string::npos);
    CHECK(r.body.find("alice") != std::string::npos);
}

// ── Login: malformed JSON credentials ────────────────────────────────────────

TEST_CASE("ClientController::Login returns BAD_REQUEST for malformed credentials", "[controller][client][login][db]")
{
    ClientController<User> ctrl;
    CR                     r;
    ctrl.Login(cap(r), anonRequester(), "not-json", "127.0.0.1");
    CHECK(r.code == Http::Status::BAD_REQUEST);
}

// ── Login: SQL injection in username ─────────────────────────────────────────

TEST_CASE("ClientController::Login returns UNAUTHORIZED for SQL injection in username", "[controller][client][login][db]")
{
    auto fake             = std::make_shared<FakeDatabaseController>();
    fake->password_result = std::nullopt;

    FakeDbGuard            guard(fake);
    ClientController<User> ctrl;
    CR                     r;
    ctrl.Login(cap(r), anonRequester(), R"({"username":"' OR 1=1 --","password":"x"})", "127.0.0.1");
    // SQL injection detected → returns UNAUTHORIZED (or BAD_REQUEST depending on path)
    CHECK((r.code == Http::Status::UNAUTHORIZED || r.code == Http::Status::BAD_REQUEST));
}

// ── Logout: valid token ───────────────────────────────────────────────────────

TEST_CASE("ClientController::Logout returns 200 for a valid token", "[controller][client][logout][db]")
{
    // Build a valid token using the test-environment keys.
    // The session manager decodes it and calls setNowLogoutTime (fire-and-forget, no return check).
    const std::string secret = "valhalla-unit-test-secret-key";
    const std::string issuer = "valhalla-unit-test-issuer";
    const std::string type   = "JWS";
    const std::string llodt  = "2026-01-01 00:00:00 +0000";

    auto token = jwt::create<jwt::traits::kazuho_picojson>()
                     .set_issuer(issuer)
                     .set_type(type)
                     .set_subject("alice")
                     .set_id("42")
                     .set_issued_at(std::chrono::system_clock::now())
                     .set_expires_at(std::chrono::system_clock::now() + std::chrono::hours{1})
                     .set_payload_claim("ip_address", jwt::basic_claim<jwt::traits::kazuho_picojson>(std::string("127.0.0.1")))
                     .set_payload_claim("llodt", jwt::basic_claim<jwt::traits::kazuho_picojson>(llodt))
                     .set_payload_claim("group", jwt::basic_claim<jwt::traits::kazuho_picojson>(std::string("users")))
                     .sign(jwt::algorithm::hs256{secret});

    auto fake = std::make_shared<FakeDatabaseController>();
    // setNowLogoutTime calls executeQuery; result is ignored, so nullopt is fine.
    fake->query_result = std::nullopt;

    FakeDbGuard            guard(fake);
    ClientController<User> ctrl;
    CR                     r;
    ctrl.Logout(cap(r), anonRequester(), token, "127.0.0.1");
    CHECK(r.code == Http::Status::OK);
    CHECK(r.body.find("success") != std::string::npos);
}

// ── Logout: empty token ───────────────────────────────────────────────────────

TEST_CASE("ClientController::Logout handles empty token gracefully", "[controller][client][logout][db]")
{
    ClientController<User> ctrl;
    CR                     r;
    ctrl.Logout(cap(r), anonRequester(), std::nullopt, "127.0.0.1");
    // Should not crash; returns some error or OK with failure message.
    CHECK(r.code != -1);
}
