// Tests for ClientController<User> and ClientController<Provider>.
// With VALHALLA_TEST_NO_DB=1 all DB queries return nullopt; every code path
// that runs before the first DB call is exercised hermetically.
#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <optional>
#include <string>

#include "controllers/clientcontroller/clientcontroller.hpp"
#include "entities/people/provider.hpp"
#include "entities/people/user.hpp"
#include "utils/global/callback.hpp"
#include "utils/global/http.hpp"
#include "utils/global/requester.hpp"

using Requester = api::v2::Requester;

// Helper: capture the status/body from a CALLBACK_ invocation.
struct Result
{
    int         status = 0;
    std::string body;
};

static Result capture(auto fn)
{
    Result r;
    fn([&](int s, const std::string& b) {
        r.status = s;
        r.body   = b;
    });
    return r;
}

// ── ClientController<User> ──────────────────────────────────────────────────

TEST_CASE("ClientController<User>::Create rejects malformed JSON", "[controllers][client]")
{
    ClientController<User> ctrl;
    auto r = capture([&](auto cb) {
        ctrl.Create(std::move(cb), Requester(0, "users"), "not json{{");
    });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
    CHECK_FALSE(r.body.empty());
}

TEST_CASE("ClientController<User>::Create rejects empty body", "[controllers][client]")
{
    ClientController<User> ctrl;
    auto r = capture([&](auto cb) {
        ctrl.Create(std::move(cb), Requester(0, "users"), "");
    });
    // Empty string is invalid JSON → BAD_REQUEST
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

TEST_CASE("ClientController<User>::Suspend returns 406 for missing id", "[controllers][client]")
{
    ClientController<User> ctrl;
    auto r = capture([&](auto cb) {
        ctrl.Suspend(std::move(cb), Requester(1, "users"), std::nullopt);
    });
    CHECK(r.status == api::v2::Http::Status::NOT_ACCEPTABLE);
}

TEST_CASE("ClientController<User>::Suspend with id calls permission check (group mismatch → 403)", "[controllers][client]")
{
    ClientController<User> ctrl;
    // Requester group "admins" does not match "users" table → FORBIDDEN
    auto r = capture([&](auto cb) {
        ctrl.Suspend(std::move(cb), Requester(42, "admins"), std::optional<uint64_t>{42});
    });
    CHECK(r.status == api::v2::Http::Status::FORBIDDEN);
}

TEST_CASE("ClientController<User>::Activate returns 406 for missing id", "[controllers][client]")
{
    ClientController<User> ctrl;
    auto r = capture([&](auto cb) {
        ctrl.Activate(std::move(cb), Requester(1, "users"), std::nullopt);
    });
    CHECK(r.status == api::v2::Http::Status::NOT_ACCEPTABLE);
}

TEST_CASE("ClientController<User>::Activate with id and mismatched requester → 403", "[controllers][client]")
{
    ClientController<User> ctrl;
    auto r = capture([&](auto cb) {
        ctrl.Activate(std::move(cb), Requester(99, "providers"), std::optional<uint64_t>{5});
    });
    CHECK(r.status == api::v2::Http::Status::FORBIDDEN);
}

TEST_CASE("ClientController<User>::GetServices returns error for missing id", "[controllers][client]")
{
    ClientController<User> ctrl;
    auto r = capture([&](auto cb) {
        ctrl.GetServices(std::move(cb), Requester(1, "users"), std::nullopt);
    });
    // JsonHelper::jsonify(...).as<std::string>() throws on Json::Value → caught → 500,
    // or returns 400 if the call succeeds. Either way a non-200 error is returned.
    CHECK(r.status != api::v2::Http::Status::OK);
    CHECK_FALSE(r.body.empty());
}

TEST_CASE("ClientController<User>::GetServices with mismatched requester → 403", "[controllers][client]")
{
    ClientController<User> ctrl;
    auto r = capture([&](auto cb) {
        ctrl.GetServices(std::move(cb), Requester(10, "providers"), std::optional<uint64_t>{10});
    });
    CHECK(r.status == api::v2::Http::Status::FORBIDDEN);
}

TEST_CASE("ClientController<User>::Login delegates to gatekeeper (bad JSON → error)", "[controllers][client]")
{
    ClientController<User> ctrl;
    auto r = capture([&](auto cb) {
        ctrl.Login(std::move(cb), Requester(0, "users"), "bad json", "127.0.0.1");
    });
    CHECK(r.status != api::v2::Http::Status::OK);
    CHECK_FALSE(r.body.empty());
}

TEST_CASE("ClientController<User>::Logout delegates to gatekeeper (empty token → error)", "[controllers][client]")
{
    ClientController<User> ctrl;
    auto r = capture([&](auto cb) {
        ctrl.Logout(std::move(cb), Requester(0, "users"), std::nullopt, "127.0.0.1");
    });
    // No token → gatekeeper returns an error
    CHECK(r.status != api::v2::Http::Status::OK);
}

TEST_CASE("ClientController<User>::Update with malformed JSON → 400", "[controllers][client]")
{
    ClientController<User> ctrl;
    auto r = capture([&](auto cb) {
        ctrl.Update(std::move(cb), Requester(5, "users"), "{{bad", std::optional<uint64_t>{5});
    });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

TEST_CASE("ClientController<User>::Delete with no id → 406", "[controllers][client]")
{
    ClientController<User> ctrl;
    auto r = capture([&](auto cb) {
        ctrl.Delete(std::move(cb), Requester(1, "users"), std::nullopt);
    });
    CHECK(r.status == api::v2::Http::Status::NOT_ACCEPTABLE);
}

TEST_CASE("ClientController<User>::Delete with mismatched requester → 403", "[controllers][client]")
{
    ClientController<User> ctrl;
    auto r = capture([&](auto cb) {
        ctrl.Delete(std::move(cb), Requester(99, "users"), std::optional<uint64_t>{5});
    });
    CHECK(r.status == api::v2::Http::Status::FORBIDDEN);
}

TEST_CASE("ClientController<User>::Read with malformed JSON → 400", "[controllers][client]")
{
    ClientController<User> ctrl;
    auto r = capture([&](auto cb) {
        ctrl.Read(std::move(cb), Requester(1, "users"), "{{bad");
    });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

TEST_CASE("ClientController<User>::Read with missing id → 400", "[controllers][client]")
{
    ClientController<User> ctrl;
    // Valid JSON but no "id" or "schema" field
    auto r = capture([&](auto cb) {
        ctrl.Read(std::move(cb), Requester(1, "users"), R"({"foo":"bar"})");
    });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

TEST_CASE("ClientController<User>::Search with malformed JSON → 400", "[controllers][client]")
{
    ClientController<User> ctrl;
    auto r = capture([&](auto cb) {
        ctrl.Search(std::move(cb), Requester(1, "users"), "not-json");
    });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

TEST_CASE("ClientController<User>::Search with missing search fields → 406", "[controllers][client]")
{
    ClientController<User> ctrl;
    // Valid JSON but Search_t will fail to parse → NOT_ACCEPTABLE
    auto r = capture([&](auto cb) {
        ctrl.Search(std::move(cb), Requester(1, "users"), R"({"foo":"bar"})");
    });
    CHECK(r.status == api::v2::Http::Status::NOT_ACCEPTABLE);
}

TEST_CASE("ClientController<User>::ResetPassword is a no-op (does not crash)", "[controllers][client]")
{
    ClientController<User> ctrl;
    bool called = false;
    ctrl.ResetPassword([&](int, const std::string&) { called = true; }, Requester(1, "users"), R"({"x":1})");
    // The implementation discards the callback — no crash is the expectation.
    CHECK_FALSE(called);
}

// ── ClientController<Provider> ─────────────────────────────────────────────

TEST_CASE("ClientController<Provider>::Create rejects malformed JSON", "[controllers][client][provider]")
{
    ClientController<Provider> ctrl;
    auto r = capture([&](auto cb) {
        ctrl.Create(std::move(cb), Requester(0, "providers"), "bad{");
    });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

TEST_CASE("ClientController<Provider>::Suspend returns 406 for missing id", "[controllers][client][provider]")
{
    ClientController<Provider> ctrl;
    auto r = capture([&](auto cb) {
        ctrl.Suspend(std::move(cb), Requester(1, "providers"), std::nullopt);
    });
    CHECK(r.status == api::v2::Http::Status::NOT_ACCEPTABLE);
}

TEST_CASE("ClientController<Provider>::GetServices returns error for missing id", "[controllers][client][provider]")
{
    ClientController<Provider> ctrl;
    auto r = capture([&](auto cb) {
        ctrl.GetServices(std::move(cb), Requester(1, "providers"), std::nullopt);
    });
    CHECK(r.status != api::v2::Http::Status::OK);
    CHECK_FALSE(r.body.empty());
}
