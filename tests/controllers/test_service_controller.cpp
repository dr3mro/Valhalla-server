// Tests for ServiceController<Clinics> — covers all 5 CRUD+Search paths.
// ServiceController is a thin wrapper over EntityController, so we focus on
// the paths that differ: permission checks use Service_t specializations.
#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "controllers/servicecontroller/servicecontroller.hpp"
#include "entities/services/clinics/clinics.hpp"
#include "entities/services/laboratories.hpp"
#include "entities/services/pharmacies.hpp"
#include "entities/services/radiologycenters.hpp"
#include "utils/global/http.hpp"
#include "utils/global/requester.hpp"

using Requester = api::v2::Requester;

struct Result { int status = 0; std::string body; };
static Result capture(auto fn)
{
    Result r;
    fn([&](int s, const std::string& b){ r.status = s; r.body = b; });
    return r;
}

// ── Create ──────────────────────────────────────────────────────────────────

TEST_CASE("ServiceController<Clinics>::Create rejects malformed JSON", "[controllers][service]")
{
    ServiceController<Clinics> ctrl;
    auto r = capture([&](auto cb){
        ctrl.Create(std::move(cb), Requester(1, "owners"), "bad{");
    });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

// ── Read ────────────────────────────────────────────────────────────────────

TEST_CASE("ServiceController<Clinics>::Read rejects malformed JSON", "[controllers][service]")
{
    ServiceController<Clinics> ctrl;
    auto r = capture([&](auto cb){
        ctrl.Read(std::move(cb), Requester(1, "owners"), "{{");
    });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

TEST_CASE("ServiceController<Clinics>::Read with missing id → 400", "[controllers][service]")
{
    ServiceController<Clinics> ctrl;
    auto r = capture([&](auto cb){
        ctrl.Read(std::move(cb), Requester(1, "owners"), R"({"schema":["name"]})");
    });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

// ── Update ──────────────────────────────────────────────────────────────────

TEST_CASE("ServiceController<Clinics>::Update with no id → 400", "[controllers][service]")
{
    ServiceController<Clinics> ctrl;
    auto r = capture([&](auto cb){
        ctrl.Update(std::move(cb), Requester(1, "owners"), R"({"name":"x"})", std::nullopt);
    });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

TEST_CASE("ServiceController<Clinics>::Update with malformed JSON → 400", "[controllers][service]")
{
    ServiceController<Clinics> ctrl;
    auto r = capture([&](auto cb){
        ctrl.Update(std::move(cb), Requester(1, "owners"), "bad{", std::optional<uint64_t>{1});
    });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

// ── Delete ──────────────────────────────────────────────────────────────────

TEST_CASE("ServiceController<Clinics>::Delete with no id → 406", "[controllers][service]")
{
    ServiceController<Clinics> ctrl;
    auto r = capture([&](auto cb){
        ctrl.Delete(std::move(cb), Requester(1, "owners"), std::nullopt);
    });
    CHECK(r.status == api::v2::Http::Status::NOT_ACCEPTABLE);
}

// ── Search ──────────────────────────────────────────────────────────────────

TEST_CASE("ServiceController<Clinics>::Search with malformed JSON → 400", "[controllers][service]")
{
    ServiceController<Clinics> ctrl;
    auto r = capture([&](auto cb){
        ctrl.Search(std::move(cb), Requester(1, "owners"), "bad{");
    });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

TEST_CASE("ServiceController<Clinics>::Search with incomplete data → 406", "[controllers][service]")
{
    ServiceController<Clinics> ctrl;
    auto r = capture([&](auto cb){
        ctrl.Search(std::move(cb), Requester(1, "owners"), R"({"x":1})");
    });
    CHECK(r.status == api::v2::Http::Status::NOT_ACCEPTABLE);
}

// ── Pharmacies / Laboratories / RadiologyCenters ────────────────────────────

TEST_CASE("ServiceController<Pharmacies>::Create rejects malformed JSON", "[controllers][service]")
{
    ServiceController<Pharmacies> ctrl;
    auto r = capture([&](auto cb){
        ctrl.Create(std::move(cb), Requester(1, "owners"), "bad{");
    });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

TEST_CASE("ServiceController<Laboratories>::Delete with no id → 406", "[controllers][service]")
{
    ServiceController<Laboratories> ctrl;
    auto r = capture([&](auto cb){
        ctrl.Delete(std::move(cb), Requester(1, "owners"), std::nullopt);
    });
    CHECK(r.status == api::v2::Http::Status::NOT_ACCEPTABLE);
}

TEST_CASE("ServiceController<RadiologyCenters>::Search with malformed JSON → 400", "[controllers][service]")
{
    ServiceController<RadiologyCenters> ctrl;
    auto r = capture([&](auto cb){
        ctrl.Search(std::move(cb), Requester(1, "owners"), "bad{");
    });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}
