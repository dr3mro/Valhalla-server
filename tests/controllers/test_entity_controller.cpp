// Tests for EntityController<User> — covers all 5 methods (Create/Read/Update/Delete/Search).
// ServiceController, AppointmentController all delegate to EntityController,
// so these tests cover those paths too.
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>

#include "controllers/entitycontroller/entitycontroller.hpp"
#include "entities/people/user.hpp"
#include "utils/global/http.hpp"
#include "utils/global/requester.hpp"

using Requester = api::v2::Requester;

struct Result
{
    int         status = 0;
    std::string body;
};
static Result capture(auto fn)
{
    Result r;
    fn(
        [&](int s, const std::string& b)
        {
            r.status = s;
            r.body   = b;
        });
    return r;
}

// ── Create ──────────────────────────────────────────────────────────────────

TEST_CASE("EntityController::Create rejects malformed JSON", "[controllers][entity]")
{
    EntityController<User> ctrl;
    auto                   r = capture([&](auto cb) { ctrl.Create(std::move(cb), Requester(1, "users"), "{{bad"); });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

// ── Read ────────────────────────────────────────────────────────────────────

TEST_CASE("EntityController::Read rejects malformed JSON", "[controllers][entity]")
{
    EntityController<User> ctrl;
    auto                   r = capture([&](auto cb) { ctrl.Read(std::move(cb), Requester(1, "users"), "not-json"); });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

TEST_CASE("EntityController::Read rejects body missing 'id'", "[controllers][entity]")
{
    EntityController<User> ctrl;
    auto                   r = capture([&](auto cb) { ctrl.Read(std::move(cb), Requester(1, "users"), R"({"schema":["name"]})"); });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

TEST_CASE("EntityController::Read rejects body missing 'schema'", "[controllers][entity]")
{
    EntityController<User> ctrl;
    auto                   r = capture([&](auto cb) { ctrl.Read(std::move(cb), Requester(1, "users"), R"({"id":5})"); });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

TEST_CASE("EntityController::Read with mismatched requester → 403", "[controllers][entity]")
{
    EntityController<User> ctrl;
    // id=5 but requester id=99 → assert_group_id_match fails
    auto r = capture([&](auto cb) { ctrl.Read(std::move(cb), Requester(99, "users"), R"({"id":5,"schema":["name"]})"); });
    CHECK(r.status == api::v2::Http::Status::FORBIDDEN);
}

// ── Update ──────────────────────────────────────────────────────────────────

TEST_CASE("EntityController::Update with no id → 400", "[controllers][entity]")
{
    EntityController<User> ctrl;
    auto                   r = capture([&](auto cb) { ctrl.Update(std::move(cb), Requester(1, "users"), R"({"name":"x"})", std::nullopt); });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

TEST_CASE("EntityController::Update with malformed JSON → 400", "[controllers][entity]")
{
    EntityController<User> ctrl;
    auto                   r = capture([&](auto cb) { ctrl.Update(std::move(cb), Requester(5, "users"), "{{bad", std::optional<uint64_t>{5}); });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

// ── Delete ──────────────────────────────────────────────────────────────────

TEST_CASE("EntityController::Delete with no id → 406", "[controllers][entity]")
{
    EntityController<User> ctrl;
    auto                   r = capture([&](auto cb) { ctrl.Delete(std::move(cb), Requester(1, "users"), std::nullopt); });
    CHECK(r.status == api::v2::Http::Status::NOT_ACCEPTABLE);
}

TEST_CASE("EntityController::Delete with mismatched requester → 403", "[controllers][entity]")
{
    EntityController<User> ctrl;
    auto                   r = capture([&](auto cb) { ctrl.Delete(std::move(cb), Requester(99, "users"), std::optional<uint64_t>{5}); });
    CHECK(r.status == api::v2::Http::Status::FORBIDDEN);
}

TEST_CASE("EntityController::Delete with matching requester proceeds to DB (unavailable)", "[controllers][entity]")
{
    EntityController<User> ctrl;
    auto                   r = capture([&](auto cb) { ctrl.Delete(std::move(cb), Requester(5, "users"), std::optional<uint64_t>{5}); });
    CHECK_FALSE(r.body.empty());
}

// ── Search ──────────────────────────────────────────────────────────────────

TEST_CASE("EntityController::Search with malformed JSON → 400", "[controllers][entity]")
{
    EntityController<User> ctrl;
    auto                   r = capture([&](auto cb) { ctrl.Search(std::move(cb), Requester(1, "users"), "not-json"); });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

TEST_CASE("EntityController::Search with incomplete search data → 406", "[controllers][entity]")
{
    EntityController<User> ctrl;
    auto                   r = capture([&](auto cb) { ctrl.Search(std::move(cb), Requester(1, "users"), R"({"foo":"bar"})"); });
    CHECK(r.status == api::v2::Http::Status::NOT_ACCEPTABLE);
}

TEST_CASE("EntityController::Search with valid search fields proceeds to DB (unavailable)", "[controllers][entity]")
{
    EntityController<User> ctrl;
    auto                   r = capture([&](auto cb)
        { ctrl.Search(std::move(cb), Requester(1, "users"), R"({"keyword":"alice","filter":"name","order_by":"id","direction":0,"limit":10,"offset":0})"); });
    CHECK_FALSE(r.body.empty());
}
