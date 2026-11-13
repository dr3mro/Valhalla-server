#include <catch2/catch_test_macros.hpp>
#include <jsoncons/json.hpp>
#include <memory>
#include <optional>
#include <string>

#include "controllers/clientcontroller/clientcontroller.hpp"
#include "entities/people/user.hpp"
#include "support/fake_db.hpp"
#include "utils/global/callback.hpp"
#include "utils/global/http.hpp"
#include "utils/global/requester.hpp"

using api::v2::Requester;
namespace Http = api::v2::Http;

// ── helpers ──────────────────────────────────────────────────────────────────

namespace
{
    struct CallResult
    {
        int         code = -1;
        std::string body;
    };

    CALLBACK_ capture(CallResult& out)
    {
        return [&out](int code, const std::string& body)
        {
            out.code = code;
            out.body = body;
        };
    }

    // A requester that owns user id=42 in group "users".
    Requester userRequester() { return Requester(42, "users"); }

    // A non-empty JSON result that makes Controller::cruds report success (200).
    jsoncons::json makeOkResult()
    {
        jsoncons::json j = jsoncons::json::object();
        j["id"]          = 42;
        return j;
    }
}  // namespace

// ── ClientController<User>::Create ───────────────────────────────────────────

TEST_CASE("ClientController::Create rejects malformed JSON", "[controller][client][create]")
{
    ClientController<User> ctrl;
    CallResult             result;
    ctrl.Create(capture(result), userRequester(), "not json {{{");
    CHECK(result.code == Http::Status::BAD_REQUEST);
}

// NOTE: ClientController::Create with valid JSON is not tested here because
// CreateClient_t calls validateDatabaseCreateSchema, which has a known UB
// bug (dereferencing end-iterator) when DatabaseSchema is empty.
// See tests/README.md, known bug 3. Those paths are tested in integration
// tests that run against a real database.

// ── ClientController<User>::Suspend ──────────────────────────────────────────

TEST_CASE("ClientController::Suspend rejects nullopt id", "[controller][client][suspend]")
{
    ClientController<User> ctrl;
    CallResult             result;
    ctrl.Suspend(capture(result), userRequester(), std::nullopt);
    CHECK(result.code == Http::Status::NOT_ACCEPTABLE);
}

TEST_CASE("ClientController::Suspend returns 400 when DB returns nullopt", "[controller][client][suspend]")
{
    auto fake          = std::make_shared<FakeDatabaseController>();
    fake->query_result = std::nullopt;  // executeQuery returns nothing

    FakeDbGuard guard(fake);
    // Controller is constructed AFTER guard so it captures the fake.
    ClientController<User> ctrl;
    CallResult             result;

    ctrl.Suspend(capture(result), Requester(42, "users"), 42u);
    // No result from DB → 400
    CHECK(result.code == Http::Status::BAD_REQUEST);
}

TEST_CASE("ClientController::Suspend returns 200 when DB succeeds", "[controller][client][suspend]")
{
    auto fake          = std::make_shared<FakeDatabaseController>();
    fake->query_result = makeOkResult();

    FakeDbGuard            guard(fake);
    ClientController<User> ctrl;
    CallResult             result;

    ctrl.Suspend(capture(result), Requester(42, "users"), 42u);
    CHECK(result.code == Http::Status::OK);
}

// ── ClientController<User>::Activate ─────────────────────────────────────────

TEST_CASE("ClientController::Activate rejects nullopt id", "[controller][client][activate]")
{
    ClientController<User> ctrl;
    CallResult             result;
    ctrl.Activate(capture(result), userRequester(), std::nullopt);
    CHECK(result.code == Http::Status::NOT_ACCEPTABLE);
}

TEST_CASE("ClientController::Activate returns 400 when DB returns nullopt", "[controller][client][activate]")
{
    auto fake          = std::make_shared<FakeDatabaseController>();
    fake->query_result = std::nullopt;

    FakeDbGuard            guard(fake);
    ClientController<User> ctrl;
    CallResult             result;

    ctrl.Activate(capture(result), Requester(42, "users"), 42u);
    CHECK(result.code == Http::Status::BAD_REQUEST);
}

TEST_CASE("ClientController::Activate returns 200 when DB succeeds", "[controller][client][activate]")
{
    auto fake          = std::make_shared<FakeDatabaseController>();
    fake->query_result = makeOkResult();

    FakeDbGuard            guard(fake);
    ClientController<User> ctrl;
    CallResult             result;

    ctrl.Activate(capture(result), Requester(42, "users"), 42u);
    CHECK(result.code == Http::Status::OK);
}

// ── ClientController<User>::GetServices ──────────────────────────────────────

TEST_CASE("ClientController::GetServices rejects nullopt id", "[controller][client][services]")
{
    ClientController<User> ctrl;
    CallResult             result;
    ctrl.GetServices(capture(result), userRequester(), std::nullopt);
    CHECK(result.code == Http::Status::BAD_REQUEST);
}

TEST_CASE("ClientController::GetServices returns empty array when DB has no results", "[controller][client][services]")
{
    auto fake           = std::make_shared<FakeDatabaseController>();
    fake->search_result = jsoncons::json::array{};  // empty array, not nullopt

    FakeDbGuard            guard(fake);
    ClientController<User> ctrl;
    CallResult             result;

    ctrl.GetServices(capture(result), Requester(42, "users"), 42u);
    CHECK(result.code == Http::Status::OK);
    CHECK(result.body == "[]");
}

TEST_CASE("ClientController::GetServices returns service array from DB", "[controller][client][services]")
{
    jsoncons::json::array services;
    jsoncons::json        svc = jsoncons::json::object();
    svc["id"]                 = 1;
    svc["name"]               = "City Clinic";
    services.push_back(svc);

    auto fake           = std::make_shared<FakeDatabaseController>();
    fake->search_result = services;

    FakeDbGuard            guard(fake);
    ClientController<User> ctrl;
    CallResult             result;

    ctrl.GetServices(capture(result), Requester(42, "users"), 42u);
    CHECK(result.code == Http::Status::OK);
    CHECK(result.body.find("City Clinic") != std::string::npos);
}

// ── ClientController<User>::Delete ───────────────────────────────────────────

TEST_CASE("ClientController::Delete rejects nullopt id", "[controller][client][delete]")
{
    ClientController<User> ctrl;
    CallResult             result;
    ctrl.Delete(capture(result), userRequester(), std::nullopt);
    CHECK(result.code == Http::Status::NOT_ACCEPTABLE);
}

TEST_CASE("ClientController::Delete rejects mismatched requester group", "[controller][client][delete]")
{
    // requester is in "providers" but entity is a User ("users")
    ClientController<User> ctrl;
    CallResult             result;
    ctrl.Delete(capture(result), Requester(42, "providers"), 42u);
    CHECK(result.code == Http::Status::FORBIDDEN);
}

TEST_CASE("ClientController::Delete returns 400 when DB returns nullopt", "[controller][client][delete]")
{
    auto fake          = std::make_shared<FakeDatabaseController>();
    fake->query_result = std::nullopt;

    FakeDbGuard            guard(fake);
    ClientController<User> ctrl;
    CallResult             result;

    ctrl.Delete(capture(result), Requester(42, "users"), 42u);
    CHECK(result.code == Http::Status::BAD_REQUEST);
}

TEST_CASE("ClientController::Delete returns 200 when DB succeeds", "[controller][client][delete]")
{
    auto fake          = std::make_shared<FakeDatabaseController>();
    fake->query_result = makeOkResult();

    FakeDbGuard            guard(fake);
    ClientController<User> ctrl;
    CallResult             result;

    ctrl.Delete(capture(result), Requester(42, "users"), 42u);
    CHECK(result.code == Http::Status::OK);
}

// ── ClientController<User>::Search ───────────────────────────────────────────

TEST_CASE("ClientController::Search rejects malformed JSON", "[controller][client][search]")
{
    ClientController<User> ctrl;
    CallResult             result;
    ctrl.Search(capture(result), userRequester(), "bad json");
    CHECK(result.code == Http::Status::BAD_REQUEST);
}

TEST_CASE("ClientController::Search rejects incomplete search params", "[controller][client][search]")
{
    // Missing "offset" field
    constexpr const char*  incomplete = R"({"keyword":"x","filter":"name","order_by":"id","direction":0,"limit":10})";
    ClientController<User> ctrl;
    CallResult             result;
    ctrl.Search(capture(result), userRequester(), incomplete);
    CHECK(result.code == Http::Status::NOT_ACCEPTABLE);
}

TEST_CASE("ClientController::Search returns results from DB", "[controller][client][search]")
{
    jsoncons::json::array rows;
    jsoncons::json        row = jsoncons::json::object();
    row["id"]                 = 1;
    row["username"]           = "alice";
    rows.push_back(row);
    // Extra row to trigger the "more results" logic (limit=1 but 2 rows returned)
    rows.push_back(row);

    auto fake           = std::make_shared<FakeDatabaseController>();
    fake->search_result = rows;

    FakeDbGuard            guard(fake);
    ClientController<User> ctrl;
    CallResult             result;

    constexpr const char* search = R"({"keyword":"alice","filter":"username","order_by":"id","direction":0,"limit":1,"offset":0})";
    ctrl.Search(capture(result), userRequester(), search);
    CHECK(result.code == Http::Status::OK);
    CHECK(result.body.find("alice") != std::string::npos);
}

TEST_CASE("ClientController::Search returns empty results when DB has no rows", "[controller][client][search]")
{
    auto fake           = std::make_shared<FakeDatabaseController>();
    fake->search_result = jsoncons::json::array{};  // empty array

    FakeDbGuard            guard(fake);
    ClientController<User> ctrl;
    CallResult             result;

    constexpr const char* search = R"({"keyword":"nobody","filter":"username","order_by":"id","direction":0,"limit":10,"offset":0})";
    ctrl.Search(capture(result), userRequester(), search);
    CHECK(result.code == Http::Status::OK);
}
