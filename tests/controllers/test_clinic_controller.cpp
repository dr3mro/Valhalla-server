/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file test_clinic_controller.cpp
 * @brief Catch2 unit tests for ClinicController's special dispatch rules.
 * @details ClinicController is not a plain wrapper: for some entity types it
 * forwards to EntityController, and for others it intentionally answers "not
 * implemented". These tests select each entity type and check which branch
 * runs. They cover Create, Delete, Search, GetVisits plus the Read/Update
 * forwarding, checking 400/403/406 status codes and the "NOT implemented" body
 * text where relevant. Bad JSON or a missing id is used so no real database is
 * needed.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * ClinicController sits behind the clinic REST endpoints (patients, visits,
 * health records, prescriptions and so on). Its value is the capability matrix:
 * only Patient/Visits support full CRUD, while the other clinic types must
 * explicitly refuse operations they do not implement. Without these tests, a
 * template or dispatch change could accidentally expose an unsupported
 * operation or disable a supported one — a dangerous ambiguity in a medical
 * records API.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE and assertions:
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - C++ class templates: https://en.cppreference.com/w/cpp/language/templates
 * - if constexpr (compile-time branching):
 * https://en.cppreference.com/w/cpp/language/if
 * - C++20 concepts / constraints:
 * https://en.cppreference.com/w/cpp/language/constraints
 * - std::string::find:
 * https://en.cppreference.com/w/cpp/string/basic_string/find
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Tests for ClinicController — covers the specialized
// CreateImpl/DeleteImpl/SearchImpl/GetVisitsImpl dispatch logic that differs
// from EntityController.
//
// ClinicController<T>::CreateImpl has two overloads:
//   - Patient / Visits     → delegates to EntityController<T>::Create
//   - all other entities   → runs validateDatabaseCreateSchema + explicit id
//   check + gateKeeper
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
//   - Patient              → checks patient_id, runs canRead, calls
//   Controller::GetVisits
//   - others               → returns 400 "GetVisit is NOT implemented"
// Provides TEST_CASE, CHECK and CHECK_FALSE.
#include <catch2/catch_test_macros.hpp>
#include <optional>  // std::optional / std::nullopt for "no id supplied" cases.
// std::string holds bodies and provides find() for text checks.
#include <string>

// The controller class under test.
#include "controllers/cliniccontroller/cliniccontroller.hpp"
// Health, a non-Patient clinic type used below.
#include "entities/services/clinics/patient/health.hpp"
// Patient, the fully supported clinic type.
#include "entities/services/clinics/patient/patient.hpp"
// PatientDrugs, another clinic entity type.
#include "entities/services/clinics/patient/patientdrugs.hpp"
// Reports, a non-Patient clinic type.
#include "entities/services/clinics/patient/reports.hpp"
// PaidServices, a visit-related type.
#include "entities/services/clinics/visits/paidservices.hpp"
// Prescriptions, a non-Patient clinic type.
#include "entities/services/clinics/visits/prescriptions.hpp"
// Requests, a visit-related type.
#include "entities/services/clinics/visits/requests.hpp"
// VisitDrugs, a visit-related type.
#include "entities/services/clinics/visits/visitdrugs.hpp"
// Visits, the other fully supported type.
#include "entities/services/clinics/visits/visits.hpp"
// Http::Status, the named status codes used in assertions.
#include "utils/global/http.hpp"
// Requester, the caller identity passed to every method.
#include "utils/global/requester.hpp"

using Requester =
    api::v2::Requester;  // A short alias for the verbose fully qualified name.

/** @brief The status and body passed to a controller callback. */
struct Result
{
  int         status = 0;  // The HTTP status; 0 means the callback never fired.
  std::string body;        // The response text, filled by the callback.
};

/** @brief Invokes @p fn with a capturing callback and returns what it received.
 */
static Result capture(auto fn)  // Converts a callback-style call into a return
                                // value the test can check.
{
  Result r;  // Default result (status 0, empty body), ready to be filled in
             // place.
  fn(  // Run the caller's lambda, passing it the capturing callback below.
      [&](int                s,
          const std::string& b)  // The callback captures r by reference so
                                 // writes here persist.
      {
        r.status = s;  // Store the status the controller reported.
        r.body   = b;  // Store the body the controller reported.
      });
  return r;  // Hand the captured answer back to the test.
}

// The dispatch is the subject, not the CRUD implementation: each case selects
// an entity type and checks which overload answered. The non-Patient/non-Visits
// branches are expected to refuse with 400 and a "NOT implemented" body.

// ── CreateImpl (non-Patient/non-Visits path) ─────────────────────────────────

// A broken body is rejected during parsing on the non-Patient Create path.
TEST_CASE("ClinicController<Health>::Create rejects malformed JSON",
          "[controllers][clinic]")
{
  ClinicController<Health>
       ctrl;  // A controller instantiated for the Health clinic type.
  auto r = capture(
      [&](auto cb)
      {
        ctrl.Create(std::move(cb), Requester(1, "providers"), "bad{json");
      });  // Invalid JSON; parsing fails before schema validation or DB work.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves the input is refused
                                              // with 400 on this branch.
}

// Note: ClinicController<Health>::Create with valid JSON reaches
// validateDatabaseCreateSchema. With no schema loaded (VALHALLA_TEST_NO_DB) the
// target table is reported "not found" and validation returns false —
// validator.cpp guards the missing-table case and wraps the parse in try/catch,
// so this is a clean 400 rather than the old empty-schema-map UB. Exercising
// the actual DB write still needs a provisioned schema, so only the JSON-parse
// failure path is covered hermetically.

// Reports is another non-Patient type; its Create must reject bad JSON the same
// way.
TEST_CASE("ClinicController<Reports>::Create rejects malformed JSON",
          "[controllers][clinic]")
{
  ClinicController<Reports> ctrl;  // A controller instantiated for Reports.
  auto                      r = capture(
      [&](auto cb)
      {
        ctrl.Create(std::move(cb), Requester(1, "providers"), "{{");
      });  // Feed invalid JSON.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves consistent validation
                                              // across non-Patient types.
}

// ── CreateImpl (Patient / Visits path — delegates to EntityController) ───────

// Patient Create delegates to EntityController, which still validates JSON
// first.
TEST_CASE(
    "ClinicController<Patient>::Create rejects malformed JSON (entity path)",
    "[controllers][clinic]")
{
  ClinicController<Patient>
       ctrl;  // A controller instantiated for Patient (the delegating branch).
  auto r = capture(
      [&](auto cb)
      {
        ctrl.Create(std::move(cb), Requester(1, "providers"), "bad{");
      });  // Invalid JSON sent down the delegation path.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves delegation does not
                                              // skip validation.
}

// Visits also delegates; bad JSON is still refused.
TEST_CASE(
    "ClinicController<Visits>::Create rejects malformed JSON (entity path)",
    "[controllers][clinic]")
{
  ClinicController<Visits> ctrl;  // A controller instantiated for Visits.
  auto                     r = capture(
      [&](auto cb)
      {
        ctrl.Create(std::move(cb), Requester(1, "providers"), "bad{");
      });  // Invalid JSON on the Visits branch.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves the Visits branch
                                              // behaves like Patient.
}

// ── DeleteImpl (non-Patient/non-Visits) → 400 "not implemented" ──────────────

// Health does not support Delete; the controller says so with 400 and a clear
// message.
TEST_CASE("ClinicController<Health>::Delete returns 400 not-implemented",
          "[controllers][clinic]")
{
  ClinicController<Health> ctrl;  // A controller instantiated for Health.
  auto                     r = capture(
      [&](auto cb)
      {
        ctrl.Delete(std::move(cb),
                    Requester(1, "providers"),
                    std::optional<uint64_t>{5});
      });  // A real-looking id; the type, not the id, decides the answer.
  CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);  // Proves unsupported
                                                          // Delete returns 400.
  CHECK(r.body.find("NOT implemented") !=
        std::string::npos);  // Proves the body explains why, so clients get a
                             // useful message.
}

// Reports likewise refuses Delete.
TEST_CASE("ClinicController<Reports>::Delete returns 400 not-implemented",
          "[controllers][clinic]")
{
  ClinicController<Reports> ctrl;  // A controller instantiated for Reports.
  auto                      r = capture(
      [&](auto cb)
      {
        ctrl.Delete(std::move(cb),
                    Requester(1, "providers"),
                    std::optional<uint64_t>{5});
      });  // Attempt a delete.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves Reports follows the
                                              // not-implemented rule.
}

// Prescriptions likewise refuses Delete.
TEST_CASE("ClinicController<Prescriptions>::Delete returns 400 not-implemented",
          "[controllers][clinic]")
{
  ClinicController<Prescriptions>
       ctrl;  // A controller instantiated for Prescriptions.
  auto r = capture(
      [&](auto cb)
      {
        ctrl.Delete(std::move(cb),
                    Requester(1, "providers"),
                    std::optional<uint64_t>{1});
      });  // Attempt a delete.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves this clinic type also
                                              // refuses Delete.
}

// ── DeleteImpl (Patient / Visits path — delegates to EntityController) ───────

// Patient Delete delegates; with no id the shared guard still returns 406.
TEST_CASE("ClinicController<Patient>::Delete with no id → 406 (entity path)",
          "[controllers][clinic]")
{
  ClinicController<Patient> ctrl;  // A controller instantiated for Patient.
  auto                      r = capture(
      [&](auto cb)
      {
        ctrl.Delete(std::move(cb), Requester(1, "providers"), std::nullopt);
      });  // No target id.
  CHECK(r.status ==
        api::v2::Http::Status::NOT_ACCEPTABLE);  // Proves the delegated path
                                                 // keeps the missing-id rule.
}

// Visits Delete delegates the same way.
TEST_CASE("ClinicController<Visits>::Delete with no id → 406 (entity path)",
          "[controllers][clinic]")
{
  ClinicController<Visits> ctrl;  // A controller instantiated for Visits.
  auto                     r = capture(
      [&](auto cb)
      {
        ctrl.Delete(std::move(cb), Requester(1, "providers"), std::nullopt);
      });  // No target id.
  CHECK(r.status ==
        api::v2::Http::Status::NOT_ACCEPTABLE);  // Proves Visits keeps the
                                                 // same rule.
}

// ── SearchImpl (non-Patient) → 400 "not implemented" ─────────────────────────

// Health does not support Search; even a well-formed query is refused with a
// message.
TEST_CASE("ClinicController<Health>::Search returns 400 not-implemented",
          "[controllers][clinic]")
{
  ClinicController<Health> ctrl;  // A controller instantiated for Health.
  auto                     r = capture(
      [&](auto cb)
      {
        ctrl.Search(std::move(cb),
                    Requester(1, "providers"),
                    R"({"keyword":"flu"})");
      });  // Parsable JSON, but the operation is unsupported for this type.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves Search is refused on
                                              // non-Patient types.
  CHECK(r.body.find("NOT implemented") !=
        std::string::npos);  // Proves the refusal includes a helpful
                             // explanation.
}

// Visits also refuses Search on the non-Patient branch.
TEST_CASE("ClinicController<Visits>::Search returns 400 not-implemented",
          "[controllers][clinic]")
{
  ClinicController<Visits> ctrl;  // A controller instantiated for Visits.
  auto                     r = capture(
      [&](auto cb)
      {
        ctrl.Search(std::move(cb),
                    Requester(1, "providers"),
                    R"({"keyword":"visit"})");
      });  // Attempt a search.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves the not-implemented
                                              // branch returns 400.
}

// ── SearchImpl (Patient) → delegates to EntityController ─────────────────────

// Patient Search delegates, so malformed JSON is still rejected.
TEST_CASE("ClinicController<Patient>::Search rejects malformed JSON",
          "[controllers][clinic]")
{
  ClinicController<Patient> ctrl;  // A controller instantiated for Patient.
  auto                      r = capture(
      [&](auto cb)
      {
        ctrl.Search(std::move(cb), Requester(1, "providers"), "bad{");
      });  // Invalid JSON.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves the delegated Search
                                              // validates input.
}

// Patient Search also rejects JSON that parses but lacks search fields.
TEST_CASE("ClinicController<Patient>::Search with missing fields → 406",
          "[controllers][clinic]")
{
  ClinicController<Patient> ctrl;  // A controller instantiated for Patient.
  auto                      r = capture(
      [&](auto cb)
      {
        ctrl.Search(std::move(cb), Requester(1, "providers"), R"({"x":1})");
      });  // Valid JSON with no recognised search fields.
  CHECK(r.status ==
        api::v2::Http::Status::NOT_ACCEPTABLE);  // Proves incomplete criteria
                                                 // are refused with 406.
}

// ── GetVisitsImpl (non-Patient) → 400 "not implemented" ──────────────────────

// Health does not support GetVisits; it answers 400 with an explanation.
TEST_CASE("ClinicController<Health>::GetVisits returns 400 not-implemented",
          "[controllers][clinic]")
{
  ClinicController<Health> ctrl;  // A controller instantiated for Health.
  auto                     r = capture(
      [&](auto cb)
      {
        ctrl.GetVisits(std::move(cb),
                       Requester(1, "providers"),
                       std::optional<uint64_t>{5});
      });  // A patient id, but the type is unsupported.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves GetVisits is
                                              // refused on this type.
  CHECK(r.body.find("NOT implemented") !=
        std::string::npos);  // Proves the body says why.
}

// Visits likewise refuses GetVisits.
TEST_CASE("ClinicController<Visits>::GetVisits returns 400 not-implemented",
          "[controllers][clinic]")
{
  ClinicController<Visits> ctrl;  // A controller instantiated for Visits.
  auto                     r = capture(
      [&](auto cb)
      {
        ctrl.GetVisits(std::move(cb),
                       Requester(1, "providers"),
                       std::optional<uint64_t>{5});
      });  // Attempt GetVisits.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves the not-implemented
                                              // branch returns 400.
}

// ── GetVisitsImpl (Patient) ──────────────────────────────────────────────────

// Patient GetVisits needs a patient id; with none it fails before any
// permission check.
TEST_CASE("ClinicController<Patient>::GetVisits with no id → 400",
          "[controllers][clinic]")
{
  ClinicController<Patient> ctrl;  // A controller instantiated for Patient.
  auto                      r = capture(
      [&](auto cb)
      {
        ctrl.GetVisits(std::move(cb), Requester(1, "providers"), std::nullopt);
      });  // No patient id supplied.
  CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);  // Proves the id guard
                                                          // returns 400.
  CHECK_FALSE(
      r.body.empty());  // Proves an explanatory message accompanies the error.
}

// A requester who is not the patient must not read that patient's visits.
TEST_CASE(
    "ClinicController<Patient>::GetVisits with mismatched requester → 403",
    "[controllers][clinic]")
{
  ClinicController<Patient> ctrl;  // A controller instantiated for Patient.
  // requester id=99 ≠ patient_id=5 → canRead fails (group/id mismatch)
  auto r = capture(
      [&](auto cb)
      {
        ctrl.GetVisits(std::move(cb),
                       Requester(99, "users"),
                       std::optional<uint64_t>{5});
      });  // Caller 99 tries to read patient 5's visits.
  CHECK(r.status ==
        api::v2::Http::Status::FORBIDDEN);  // Proves the read-permission
                                            // check blocks access with 403.
}

// ── Read / Update delegate through EntityController ─────────────────────────

// Read is inherited from EntityController; bad JSON is refused.
TEST_CASE("ClinicController<Health>::Read rejects malformed JSON",
          "[controllers][clinic]")
{
  ClinicController<Health> ctrl;  // A controller instantiated for Health.
  auto                     r = capture(
      [&](auto cb)
      {
        ctrl.Read(std::move(cb), Requester(1, "providers"), "bad{");
      });  // Invalid JSON body.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves the delegated
                                              // Read still validates.
}

// Update needs an id; without one it fails even for a fully supported type.
TEST_CASE("ClinicController<Patient>::Update with no id → 400",
          "[controllers][clinic]")
{
  ClinicController<Patient> ctrl;  // A controller instantiated for Patient.
  auto                      r = capture(
      [&](auto cb)
      {
        ctrl.Update(std::move(cb),
                    Requester(1, "providers"),
                    R"({"name":"x"})",
                    std::nullopt);
      });  // Valid JSON but no id argument.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves the missing-id guard
                                              // fires before any update work.
}
