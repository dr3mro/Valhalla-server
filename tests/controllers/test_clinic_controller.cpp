// Tests for ClinicController — covers the specialized CreateImpl/DeleteImpl/SearchImpl/GetVisitsImpl
// dispatch logic that differs from EntityController.
//
// ClinicController<T>::CreateImpl has two overloads:
//   - Patient / Visits     → delegates to EntityController<T>::Create
//   - all other entities   → runs validateDatabaseCreateSchema + explicit id check + gateKeeper
//
// DeleteImpl:
//   - Patient / Visits     → delegates to EntityController<T>::Delete
//   - others               → returns 400 "Delete is NOT implemented"
//
// SearchImpl:
//   - Patient              → delegates to EntityController<T>::Search
//   - others               → returns 400 "Search is NOT implemented"
//
// GetVisitsImpl:
//   - Patient              → checks patient_id, runs canRead, calls Controller::GetVisits
//   - others               → returns 400 "GetVisit is NOT implemented"
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>

#include "controllers/cliniccontroller/cliniccontroller.hpp"
#include "entities/services/clinics/patient/health.hpp"
#include "entities/services/clinics/patient/patient.hpp"
#include "entities/services/clinics/patient/patientdrugs.hpp"
#include "entities/services/clinics/patient/reports.hpp"
#include "entities/services/clinics/visits/paidservices.hpp"
#include "entities/services/clinics/visits/prescriptions.hpp"
#include "entities/services/clinics/visits/requests.hpp"
#include "entities/services/clinics/visits/visitdrugs.hpp"
#include "entities/services/clinics/visits/visits.hpp"
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

// ── CreateImpl (non-Patient/non-Visits path) ─────────────────────────────────

TEST_CASE("ClinicController<Health>::Create rejects malformed JSON", "[controllers][clinic]")
{
    ClinicController<Health> ctrl;
    auto                     r = capture([&](auto cb) { ctrl.Create(std::move(cb), Requester(1, "providers"), "bad{json"); });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

// Note: ClinicController<Health>::Create with valid JSON hits validateDatabaseCreateSchema
// which dereferences the empty schema map in no-DB mode (UB). Those paths require a live DB.
// Only the JSON-parse failure path is safely testable hermetically.

TEST_CASE("ClinicController<Reports>::Create rejects malformed JSON", "[controllers][clinic]")
{
    ClinicController<Reports> ctrl;
    auto                      r = capture([&](auto cb) { ctrl.Create(std::move(cb), Requester(1, "providers"), "{{"); });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

// ── CreateImpl (Patient / Visits path — delegates to EntityController) ───────

TEST_CASE("ClinicController<Patient>::Create rejects malformed JSON (entity path)", "[controllers][clinic]")
{
    ClinicController<Patient> ctrl;
    auto                      r = capture([&](auto cb) { ctrl.Create(std::move(cb), Requester(1, "providers"), "bad{"); });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

TEST_CASE("ClinicController<Visits>::Create rejects malformed JSON (entity path)", "[controllers][clinic]")
{
    ClinicController<Visits> ctrl;
    auto                     r = capture([&](auto cb) { ctrl.Create(std::move(cb), Requester(1, "providers"), "bad{"); });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

// ── DeleteImpl (non-Patient/non-Visits) → 400 "not implemented" ──────────────

TEST_CASE("ClinicController<Health>::Delete returns 400 not-implemented", "[controllers][clinic]")
{
    ClinicController<Health> ctrl;
    auto                     r = capture([&](auto cb) { ctrl.Delete(std::move(cb), Requester(1, "providers"), std::optional<uint64_t>{5}); });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
    CHECK(r.body.find("NOT implemented") != std::string::npos);
}

TEST_CASE("ClinicController<Reports>::Delete returns 400 not-implemented", "[controllers][clinic]")
{
    ClinicController<Reports> ctrl;
    auto                      r = capture([&](auto cb) { ctrl.Delete(std::move(cb), Requester(1, "providers"), std::optional<uint64_t>{5}); });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

TEST_CASE("ClinicController<Prescriptions>::Delete returns 400 not-implemented", "[controllers][clinic]")
{
    ClinicController<Prescriptions> ctrl;
    auto                            r = capture([&](auto cb) { ctrl.Delete(std::move(cb), Requester(1, "providers"), std::optional<uint64_t>{1}); });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

// ── DeleteImpl (Patient / Visits path — delegates to EntityController) ───────

TEST_CASE("ClinicController<Patient>::Delete with no id → 406 (entity path)", "[controllers][clinic]")
{
    ClinicController<Patient> ctrl;
    auto                      r = capture([&](auto cb) { ctrl.Delete(std::move(cb), Requester(1, "providers"), std::nullopt); });
    CHECK(r.status == api::v2::Http::Status::NOT_ACCEPTABLE);
}

TEST_CASE("ClinicController<Visits>::Delete with no id → 406 (entity path)", "[controllers][clinic]")
{
    ClinicController<Visits> ctrl;
    auto                     r = capture([&](auto cb) { ctrl.Delete(std::move(cb), Requester(1, "providers"), std::nullopt); });
    CHECK(r.status == api::v2::Http::Status::NOT_ACCEPTABLE);
}

// ── SearchImpl (non-Patient) → 400 "not implemented" ─────────────────────────

TEST_CASE("ClinicController<Health>::Search returns 400 not-implemented", "[controllers][clinic]")
{
    ClinicController<Health> ctrl;
    auto                     r = capture([&](auto cb) { ctrl.Search(std::move(cb), Requester(1, "providers"), R"({"keyword":"flu"})"); });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
    CHECK(r.body.find("NOT implemented") != std::string::npos);
}

TEST_CASE("ClinicController<Visits>::Search returns 400 not-implemented", "[controllers][clinic]")
{
    ClinicController<Visits> ctrl;
    auto                     r = capture([&](auto cb) { ctrl.Search(std::move(cb), Requester(1, "providers"), R"({"keyword":"visit"})"); });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

// ── SearchImpl (Patient) → delegates to EntityController ─────────────────────

TEST_CASE("ClinicController<Patient>::Search rejects malformed JSON", "[controllers][clinic]")
{
    ClinicController<Patient> ctrl;
    auto                      r = capture([&](auto cb) { ctrl.Search(std::move(cb), Requester(1, "providers"), "bad{"); });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

TEST_CASE("ClinicController<Patient>::Search with missing fields → 406", "[controllers][clinic]")
{
    ClinicController<Patient> ctrl;
    auto                      r = capture([&](auto cb) { ctrl.Search(std::move(cb), Requester(1, "providers"), R"({"x":1})"); });
    CHECK(r.status == api::v2::Http::Status::NOT_ACCEPTABLE);
}

// ── GetVisitsImpl (non-Patient) → 400 "not implemented" ──────────────────────

TEST_CASE("ClinicController<Health>::GetVisits returns 400 not-implemented", "[controllers][clinic]")
{
    ClinicController<Health> ctrl;
    auto                     r = capture([&](auto cb) { ctrl.GetVisits(std::move(cb), Requester(1, "providers"), std::optional<uint64_t>{5}); });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
    CHECK(r.body.find("NOT implemented") != std::string::npos);
}

TEST_CASE("ClinicController<Visits>::GetVisits returns 400 not-implemented", "[controllers][clinic]")
{
    ClinicController<Visits> ctrl;
    auto                     r = capture([&](auto cb) { ctrl.GetVisits(std::move(cb), Requester(1, "providers"), std::optional<uint64_t>{5}); });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

// ── GetVisitsImpl (Patient) ──────────────────────────────────────────────────

TEST_CASE("ClinicController<Patient>::GetVisits with no id → 400", "[controllers][clinic]")
{
    ClinicController<Patient> ctrl;
    auto                      r = capture([&](auto cb) { ctrl.GetVisits(std::move(cb), Requester(1, "providers"), std::nullopt); });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
    CHECK_FALSE(r.body.empty());
}

TEST_CASE("ClinicController<Patient>::GetVisits with mismatched requester → 403", "[controllers][clinic]")
{
    ClinicController<Patient> ctrl;
    // requester id=99 ≠ patient_id=5 → canRead fails (group/id mismatch)
    auto r = capture([&](auto cb) { ctrl.GetVisits(std::move(cb), Requester(99, "users"), std::optional<uint64_t>{5}); });
    CHECK(r.status == api::v2::Http::Status::FORBIDDEN);
}

// ── Read / Update delegate through EntityController ─────────────────────────

TEST_CASE("ClinicController<Health>::Read rejects malformed JSON", "[controllers][clinic]")
{
    ClinicController<Health> ctrl;
    auto                     r = capture([&](auto cb) { ctrl.Read(std::move(cb), Requester(1, "providers"), "bad{"); });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

TEST_CASE("ClinicController<Patient>::Update with no id → 400", "[controllers][clinic]")
{
    ClinicController<Patient> ctrl;
    auto                      r = capture([&](auto cb) { ctrl.Update(std::move(cb), Requester(1, "providers"), R"({"name":"x"})", std::nullopt); });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}
