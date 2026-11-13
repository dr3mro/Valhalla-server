// Tests for KeeprBase::validateToken via FakeDatabaseController.
// validateToken is protected; exposed through TestKeeprValidate below.
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <jsoncons/json.hpp>
#include <optional>
#include <string>

#include "gatekeeper/keeprbase/keeprbase.hpp"
#include "support/builders.hpp"
#include "support/fake_db.hpp"

namespace
{
    // Minimal subclass that promotes validateToken to public.
    struct TestKeeprValidate : api::v2::KeeprBase
    {
        using KeeprBase::validateToken;
    };

    constexpr const char* kSecret = "valhalla-unit-test-secret-key";
    constexpr const char* kIssuer = "valhalla-unit-test-issuer";
    constexpr const char* kType   = "JWS";

    /// Builds a JSON row that getLastLogoutTimeIfActive() expects:
    /// { "id": <id>, "last_logout": <llodt>, "active": <active> }
    jsoncons::json makeSessionRow(uint64_t id, const std::string& llodt, bool active)
    {
        jsoncons::json row = jsoncons::json::object();
        row["id"]          = id;
        row["last_logout"] = llodt;
        row["active"]      = active;
        return row;
    }
}  // namespace

TEST_CASE("KeeprBase::validateToken returns false when llodt mismatches DB value", "[gatekeeper][keeprbase][validatetoken]")
{
    auto        fake = std::make_shared<FakeDatabaseController>();
    FakeDbGuard guard(fake);

    const std::string tokenLlodt = "2026-01-01 00:00:00 +0000";
    const std::string dbLlodt    = "2026-06-15 12:00:00 +0000";  // different

    std::optional<api::v2::Types::ClientLoginData> data = valhalla_test::makeLoginData(700, "users", "127.0.0.1");
    data->username                                      = "user";
    data->lastLogoutTime                                = tokenLlodt;
    data->token                                         = valhalla_test::makeToken(kSecret, kIssuer, kType, "user", 700, "users", "127.0.0.1", tokenLlodt);

    // DB returns a different llodt → validation must fail.
    fake->read_result = makeSessionRow(700, dbLlodt, true);

    TestKeeprValidate k;
    auto              decoded = jwt::decode<jwt::traits::kazuho_picojson>(data->token.value());
    std::string       message;
    CHECK_FALSE(k.validateToken(data, message, decoded));
    CHECK(message.find("logged out") != std::string::npos);
}

TEST_CASE("KeeprBase::validateToken returns false when account is inactive", "[gatekeeper][keeprbase][validatetoken]")
{
    auto        fake = std::make_shared<FakeDatabaseController>();
    FakeDbGuard guard(fake);

    const std::string llodt = "2026-01-01 00:00:00 +0000";

    std::optional<api::v2::Types::ClientLoginData> data = valhalla_test::makeLoginData(701, "users", "127.0.0.1");
    data->username                                      = "user";
    data->lastLogoutTime                                = llodt;
    data->token                                         = valhalla_test::makeToken(kSecret, kIssuer, kType, "user", 701, "users", "127.0.0.1", llodt);

    // llodt matches but account is suspended.
    fake->read_result = makeSessionRow(701, llodt, /*active=*/false);

    TestKeeprValidate k;
    auto              decoded = jwt::decode<jwt::traits::kazuho_picojson>(data->token.value());
    std::string       message;
    CHECK_FALSE(k.validateToken(data, message, decoded));
    CHECK(message.find("suspend") != std::string::npos);
}

TEST_CASE("KeeprBase::validateToken proceeds with caller-set values when DB returns no row", "[gatekeeper][keeprbase][validatetoken]")
{
    // validateToken ignores the bool returned by getLastLogoutTimeIfActive(), so
    // when the DB returns nullopt it falls through using whatever the caller
    // pre-set in clientLoginData. With llodt and active matching, it succeeds.
    // This documents the production behaviour: missing session row is not an
    // automatic rejection.
    auto        fake = std::make_shared<FakeDatabaseController>();
    FakeDbGuard guard(fake);

    const std::string llodt = "2026-01-01 00:00:00 +0000";

    std::optional<api::v2::Types::ClientLoginData> data = valhalla_test::makeLoginData(702, "users", "127.0.0.1");
    data->username                                      = "user";
    data->lastLogoutTime                                = llodt;
    data->is_active                                     = true;
    data->token                                         = valhalla_test::makeToken(kSecret, kIssuer, kType, "user", 702, "users", "127.0.0.1", llodt);

    // DB returns nothing.
    fake->read_result = std::nullopt;

    TestKeeprValidate k;
    auto              decoded = jwt::decode<jwt::traits::kazuho_picojson>(data->token.value());
    std::string       message;
    // Should succeed because pre-set llodt matches token and is_active == true.
    CHECK(k.validateToken(data, message, decoded));
}

TEST_CASE("KeeprBase::validateToken returns false for a structurally malformed token", "[gatekeeper][keeprbase][validatetoken]")
{
    auto        fake = std::make_shared<FakeDatabaseController>();
    FakeDbGuard guard(fake);

    const std::string llodt = "2026-01-01 00:00:00 +0000";

    std::optional<api::v2::Types::ClientLoginData> data = valhalla_test::makeLoginData(703, "users", "127.0.0.1");
    data->username                                      = "user";
    data->lastLogoutTime                                = llodt;

    // A token with a tampered signature.
    std::string good = valhalla_test::makeToken(kSecret, kIssuer, kType, "user", 703, "users", "127.0.0.1", llodt);
    data->token      = valhalla_test::makeTamperedToken(good);

    fake->read_result = makeSessionRow(703, llodt, true);

    TestKeeprValidate k;
    // jwt::decode on a tampered token may throw or produce a decodable token
    // that fails verify(). Either way validateToken must not succeed.
    try
    {
        auto        decoded = jwt::decode<jwt::traits::kazuho_picojson>(data->token.value());
        std::string message;
        CHECK_FALSE(k.validateToken(data, message, decoded));
    }
    catch (...)
    {
        // decode itself threw — that's also acceptable (token was malformed).
        SUCCEED("jwt::decode threw for tampered token");
    }
}

TEST_CASE("KeeprBase::validateToken returns true for a valid active token", "[gatekeeper][keeprbase][validatetoken]")
{
    auto        fake = std::make_shared<FakeDatabaseController>();
    FakeDbGuard guard(fake);

    const std::string llodt = "2026-01-01 00:00:00 +0000";

    std::optional<api::v2::Types::ClientLoginData> data = valhalla_test::makeLoginData(704, "users", "127.0.0.1");
    data->username                                      = "user";
    data->lastLogoutTime                                = llodt;
    data->token                                         = valhalla_test::makeToken(kSecret, kIssuer, kType, "user", 704, "users", "127.0.0.1", llodt);

    // llodt matches, account is active.
    fake->read_result = makeSessionRow(704, llodt, true);

    TestKeeprValidate k;
    auto              decoded = jwt::decode<jwt::traits::kazuho_picojson>(data->token.value());
    std::string       message;
    CHECK(k.validateToken(data, message, decoded));
    CHECK(message.empty());
}
