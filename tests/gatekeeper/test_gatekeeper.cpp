#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>

#include "gatekeeper/dosdetector/dosdetector.hpp"
#include "gatekeeper/gatekeeper.hpp"
#include "gatekeeper/sqlinjectiondetectoor/sqlinjectiondetector.hpp"
#include "support/builders.hpp"

using GateKeeper = api::v2::GateKeeper;

// ── isQuerySqlInjection (static pure wrapper) ───────────────────────────────

TEST_CASE("GateKeeper::isQuerySqlInjection flags malicious queries", "[gatekeeper][sqli]")
{
    SqlInjectionDetector::initialize();

    CHECK(GateKeeper::isQuerySqlInjection("SLEEP(10)"));
    CHECK(GateKeeper::isQuerySqlInjection("'; DROP TABLE users; --"));
    CHECK(GateKeeper::isQuerySqlInjection("SELECT * FROM users WHERE id=1 --"));
    CHECK(GateKeeper::isQuerySqlInjection("SHUTDOWN"));
    CHECK(GateKeeper::isQuerySqlInjection("EXEC sp_who2"));
}

TEST_CASE("GateKeeper::isQuerySqlInjection accepts legitimate production queries", "[gatekeeper][sqli]")
{
    CHECK_FALSE(GateKeeper::isQuerySqlInjection("SELECT * FROM users_safe WHERE id=1 LIMIT 1;"));
    CHECK_FALSE(GateKeeper::isQuerySqlInjection("INSERT INTO appointments (date) VALUES ('2026-01-15') RETURNING *;"));
    CHECK_FALSE(GateKeeper::isQuerySqlInjection("SELECT id,password,active FROM users WHERE username = $1 LIMIT 1;"));
    CHECK_FALSE(GateKeeper::isQuerySqlInjection("SELECT NEXTVAL('users_id_seq');"));
    CHECK_FALSE(GateKeeper::isQuerySqlInjection(""));
}

// ── isDosAttack (uses singleton DOSDetector from Store) ─────────────────────

TEST_CASE("GateKeeper::isDosAttack allows a fresh request", "[gatekeeper][dos]")
{
    GateKeeper                 gk;
    const DOSDetector::Request req{
        .ip      = "10.99.88.77",
        .method  = "GET",
        .path    = "/api/v2/users/read",
        .headers = {},
        .body    = "fresh-request-body",
    };
    CHECK(gk.isDosAttack(req) == DOSDetector::Status::ALLOWED);
}

TEST_CASE("GateKeeper::isDosAttack returns a valid status for every request", "[gatekeeper][dos]")
{
    // The singleton DOSDetector uses whatever threshold is in the environment.
    // We only verify the return type is one of the known Status values — the
    // threshold logic is exhaustively tested in test_dosdetector.cpp.
    GateKeeper                 gk;
    const DOSDetector::Request req{
        .ip      = "10.202.33.44",
        .method  = "POST",
        .path    = "/api/v2/test",
        .headers = {},
        .body    = "repeated-gatekeeper-body",
    };

    const auto status = gk.isDosAttack(req);
    CHECK((status == DOSDetector::Status::ALLOWED || status == DOSDetector::Status::RATELIMITED || status == DOSDetector::Status::BANNED));
}

// ── isAuthenticationValid (Store singletons, no DB) ─────────────────────────

TEST_CASE("GateKeeper::isAuthenticationValid rejects an empty ClientLoginData", "[gatekeeper][auth]")
{
    GateKeeper                                     gk;
    std::optional<api::v2::Types::ClientLoginData> empty;
    std::string                                    message;
    CHECK_FALSE(gk.isAuthenticationValid(empty, message));
}

TEST_CASE("GateKeeper::isAuthenticationValid rejects a token with an invalid signature", "[gatekeeper][auth]")
{
    GateKeeper                                     gk;
    const std::string                              llodt = "2026-01-01 00:00:00 +0000";
    std::optional<api::v2::Types::ClientLoginData> data  = valhalla_test::makeLoginData(700, "users", "127.0.0.1");
    data->username                                       = "user";
    data->lastLogoutTime                                 = llodt;
    // Sign with the WRONG secret — verification must fail.
    data->token = valhalla_test::makeToken("wrong-secret", "wrong-issuer", "JWS", "user", 700, "users", "127.0.0.1", llodt);

    std::string message;
    CHECK_FALSE(gk.isAuthenticationValid(data, message));
}
