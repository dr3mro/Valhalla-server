// Tests for StaffController<Clinics> — covers AddStaff/RemoveStaff/InviteStaff.
// AddStaff/RemoveStaff have no pre-parse guard: bad JSON throws → CRITICALMESSAGERESPONSE → 500.
// InviteStaff similarly throws on bad JSON → 500.
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "controllers/staffcontroller/staffcontroller.hpp"
#include "entities/services/clinics/clinics.hpp"
#include "entities/services/laboratories.hpp"
#include "entities/services/pharmacies.hpp"
#include "entities/services/radiologycenters.hpp"
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

// ── AddStaffToEntity ─────────────────────────────────────────────────────────

TEST_CASE("StaffController<Clinics>::AddStaffToEntity with malformed JSON → 500", "[controllers][staff]")
{
    StaffController<Clinics> ctrl;
    auto                     r = capture([&](auto cb) { ctrl.AddStaffToEntity(std::move(cb), Requester(1, "owners"), "bad{"); });
    CHECK(r.status == api::v2::Http::Status::INTERNAL_SERVER_ERROR);
    CHECK_FALSE(r.body.empty());
}

TEST_CASE("StaffController<Clinics>::AddStaffToEntity with missing 'payload' key → 500", "[controllers][staff]")
{
    StaffController<Clinics> ctrl;
    auto                     r = capture([&](auto cb) { ctrl.AddStaffToEntity(std::move(cb), Requester(1, "owners"), R"({"no_payload":1})"); });
    // at("payload") throws → CRITICALMESSAGERESPONSE
    CHECK(r.status == api::v2::Http::Status::INTERNAL_SERVER_ERROR);
}

TEST_CASE("StaffController<Clinics>::AddStaffToEntity with valid payload proceeds to DB", "[controllers][staff]")
{
    StaffController<Clinics> ctrl;
    // Minimal valid payload for StaffData
    auto r = capture([&](auto cb) { ctrl.AddStaffToEntity(std::move(cb), Requester(1, "owners"), R"({"payload":{"staff_id":10,"role":"nurse"}})"); });
    // Passes JSON parse; reaches DB (unavailable) or returns a response — must not crash
    CHECK_FALSE(r.body.empty());
}

// ── RemoveStaffFromEntity ────────────────────────────────────────────────────

TEST_CASE("StaffController<Clinics>::RemoveStaffFromEntity with malformed JSON → 500", "[controllers][staff]")
{
    StaffController<Clinics> ctrl;
    auto                     r = capture([&](auto cb) { ctrl.RemoveStaffFromEntity(std::move(cb), Requester(1, "owners"), "bad{"); });
    CHECK(r.status == api::v2::Http::Status::INTERNAL_SERVER_ERROR);
}

TEST_CASE("StaffController<Clinics>::RemoveStaffFromEntity with missing 'id' → 500", "[controllers][staff]")
{
    StaffController<Clinics> ctrl;
    auto                     r = capture([&](auto cb) { ctrl.RemoveStaffFromEntity(std::move(cb), Requester(1, "owners"), R"({"payload":{"staff_id":10}})"); });
    // at("id") throws → 500
    CHECK(r.status == api::v2::Http::Status::INTERNAL_SERVER_ERROR);
}

TEST_CASE("StaffController<Clinics>::RemoveStaffFromEntity with mismatched requester returns error", "[controllers][staff]")
{
    StaffController<Clinics> ctrl;
    // requester id=99, service id=1 → canManageStaff fails; DB unavailable → 400
    auto r =
        capture([&](auto cb) { ctrl.RemoveStaffFromEntity(std::move(cb), Requester(99, "users"), R"({"id":1,"payload":{"staff_id":10,"role":"nurse"}})"); });
    CHECK(r.status != api::v2::Http::Status::OK);
    CHECK_FALSE(r.body.empty());
}

// ── InviteStaffToEntity ──────────────────────────────────────────────────────

TEST_CASE("StaffController<Clinics>::InviteStaffToEntity with malformed JSON → 500", "[controllers][staff]")
{
    StaffController<Clinics> ctrl;
    auto                     r = capture([&](auto cb) { ctrl.InviteStaffToEntity(std::move(cb), Requester(1, "owners"), "bad{"); });
    CHECK(r.status == api::v2::Http::Status::INTERNAL_SERVER_ERROR);
}

TEST_CASE("StaffController<Clinics>::InviteStaffToEntity with missing 'nominee_id' → 500", "[controllers][staff]")
{
    StaffController<Clinics> ctrl;
    auto                     r = capture([&](auto cb) { ctrl.InviteStaffToEntity(std::move(cb), Requester(1, "owners"), R"({"no_nominee":1})"); });
    CHECK(r.status == api::v2::Http::Status::INTERNAL_SERVER_ERROR);
}

TEST_CASE("StaffController<Clinics>::InviteStaffToEntity with mismatched requester returns error", "[controllers][staff]")
{
    StaffController<Clinics> ctrl;
    // requester id=99 ≠ nominee_id=5; canManageStaff fails; DB unavailable → 400
    auto r = capture(
        [&](auto cb)
        {
            ctrl.InviteStaffToEntity(
                std::move(cb), Requester(99, "users"), R"({"nominee_id":5,"staff_id":5,"role":"nurse","service_id":1,"service_name":"clinics"})");
        });
    CHECK(r.status != api::v2::Http::Status::OK);
    CHECK_FALSE(r.body.empty());
}

// ── Pharmacies / Laboratories / RadiologyCenters ─────────────────────────────

TEST_CASE("StaffController<Pharmacies>::AddStaffToEntity with malformed JSON → 500", "[controllers][staff]")
{
    StaffController<Pharmacies> ctrl;
    auto                        r = capture([&](auto cb) { ctrl.AddStaffToEntity(std::move(cb), Requester(1, "owners"), "bad{"); });
    CHECK(r.status == api::v2::Http::Status::INTERNAL_SERVER_ERROR);
}

TEST_CASE("StaffController<Laboratories>::RemoveStaffFromEntity with malformed JSON → 500", "[controllers][staff]")
{
    StaffController<Laboratories> ctrl;
    auto                          r = capture([&](auto cb) { ctrl.RemoveStaffFromEntity(std::move(cb), Requester(1, "owners"), "bad{"); });
    CHECK(r.status == api::v2::Http::Status::INTERNAL_SERVER_ERROR);
}

TEST_CASE("StaffController<RadiologyCenters>::InviteStaffToEntity with malformed JSON → 500", "[controllers][staff]")
{
    StaffController<RadiologyCenters> ctrl;
    auto                              r = capture([&](auto cb) { ctrl.InviteStaffToEntity(std::move(cb), Requester(1, "owners"), "bad{"); });
    CHECK(r.status == api::v2::Http::Status::INTERNAL_SERVER_ERROR);
}
