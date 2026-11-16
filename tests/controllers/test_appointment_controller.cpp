/**
 * @file test_appointment_controller.cpp
 * @brief Catch2 unit tests and fixtures for appointment controller.
 * @details These tests check that AppointmentController forwards every
 * operation (Create, Read, Update, Delete and Search) to EntityController and
 * that bad input is rejected before the real work begins. Each case
 * deliberately sends broken JSON or a missing id/schema, so the controller
 * answers with an HTTP error status (400 Bad Request or 406 Not Acceptable) and
 * never reaches the gatekeeper or the database. Because nothing external is
 * touched, the whole suite runs with no fixture, no environment variable and no
 * server.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * AppointmentController is the thin layer behind the appointments REST
 * endpoints (clinic, pharmacy, laboratory and radiology-center appointments).
 * It protects against a regression where a malformed client request slips
 * through validation and either crashes the server or queries the database with
 * garbage. It also pins down the deliberate difference between Delete (406 for
 * a missing id) and the other operations (400), so a future refactor cannot
 * silently change the status-code contract that clients rely on.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE and tags:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Catch2 CHECK assertions:
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - C++ class templates: https://en.cppreference.com/w/cpp/language/templates
 * - std::optional (a value that may be empty):
 * https://en.cppreference.com/w/cpp/utility/optional
 * - C++ lambdas (small inline functions):
 * https://en.cppreference.com/w/cpp/language/lambda
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Tests for AppointmentController — covers Create/Read/Update/Delete/Search
// paths. AppointmentController delegates every method verbatim to
// EntityController, so these cases pin down that delegation plus the
// pre-database input validation. All inputs are malformed or incomplete:
// gatekeeper and the DB are never reached, so the suite needs no fixtures or
// environment.
// Provides TEST_CASE (declare a test) and CHECK (assert a condition).
#include <catch2/catch_test_macros.hpp>
// std::optional, used to pass an empty "no id given" value to Delete/Update.
#include <optional>
// std::string, used to hold the response body captured from the controller.
#include <string>

// The controller class under test.
#include "controllers/appointmentcontroller/appointmentcontroller.hpp"
// ClinicAppointment, the main template argument used below.
#include "entities/appointments/clinic.hpp"
// LaboratoryAppointment, used in one delegation case.
#include "entities/appointments/laboratory.hpp"
// PharmacyAppointment, used in one delegation case.
#include "entities/appointments/pharmacy.hpp"
// RadiologyCenterAppointment, used in one case.
#include "entities/appointments/radiologycenter.hpp"
// Http::Status, the named status codes the assertions compare against.
#include "utils/global/http.hpp"
// Requester, the caller identity handed to every method.
#include "utils/global/requester.hpp"

using Requester = api::v2::Requester;  // A short local name for the fully
                                       // qualified requester type.

/**
 * @brief The status HTTP code and the body text a controller produced.
 * @details Controllers do not return their answer; they hand it to a callback.
 * This tiny two-field struct is a convenient "envelope" to fill from that
 * callback so a test can check the result with ordinary assertions.
 */
struct Result {
  int status = 0;  // The HTTP status code; starts at 0 so an un-called
                   // callback is obvious.
  std::string
      body;  // The response text; starts empty and is filled by the callback.
};
/**
 * @brief Invokes @p fn with a capturing callback and returns what it received.
 * @details The controller reports its answer by "calling back" a function with
 * a status code and a body. This helper makes a lambda that writes those two
 * values into a Result, passes it to @p fn, and then returns the filled Result.
 * That lets each test say "call the controller and give me its answer" in a
 * single line.
 * @param fn A callable that takes the callback and drives one controller
 * method.
 * @return The status and body the controller passed to the callback.
 */
// Turns a callback-style controller call into a
// convenient returned value.
static Result capture(auto fn) {
  Result r;  // A default result (status 0, empty body), ready to be filled by
             // the callback.
  // Run the caller's lambda, giving it the capturing callback defined
  // next.
  // The callback: captures r by reference so
  // it can fill it in place.
  fn([&](int s, const std::string& b) {
    r.status = s;  // Save the HTTP status the controller decided on.
    r.body   = b;  // Save the response text the controller produced.
  });
  return r;  // Hand the captured status and body back to the test for its
             // assertions.
}

// Malformed JSON and missing id/schema are rejected with 400 before any DB
// work; Delete alone uses 406 for a missing id. The other three appointment
// types get one representative case each, confirming they share the same
// delegation.

// ── ClinicAppointment ────────────────────────────────────────────────────────

// Create with a broken JSON body must be refused up front. This proves the
// parse-and-validate step runs before any entity is built or any SQL is issued.
TEST_CASE(
    "AppointmentController<ClinicAppointment>::Create rejects malformed JSON",
    "[controllers][appointment]") {
  AppointmentController<ClinicAppointment>
       ctrl;  // A fresh controller for clinic appointments; it holds no
              // per-request state.
  auto r = capture([&](auto cb) {
    ctrl.Create(std::move(cb), Requester(1, "users"), "bad{");
  });  // Call Create with invalid JSON and capture the reply; std::move
       // hands the callback over without copying it.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves malformed JSON yields
                                              // 400, not a crash or a
                                              // database call.
}

// Read with broken JSON must also be rejected before looking up any record.
TEST_CASE(
    "AppointmentController<ClinicAppointment>::Read rejects malformed JSON",
    "[controllers][appointment]") {
  AppointmentController<ClinicAppointment>
       ctrl;  // A fresh clinic-appointment controller for this case.
  auto r = capture([&](auto cb) {
    ctrl.Read(std::move(cb), Requester(1, "users"), "{{");
  });  // Feed Read invalid JSON ("{{") and capture its reply.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves an unparsable body is
                                              // refused with 400.
}

// A syntactically valid body is still invalid if the required "id" is missing.
TEST_CASE(
    "AppointmentController<ClinicAppointment>::Read with missing id → 400",
    "[controllers][appointment]") {
  AppointmentController<ClinicAppointment>
       ctrl;  // A fresh clinic-appointment controller.
  auto r = capture([&](auto cb) {
    ctrl.Read(std::move(cb), Requester(1, "users"), R"({"schema":["date"]})");
  });  // Valid JSON with a schema but no id; R"(...)" is a raw string so
       // the quotes need no escaping.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves Read requires an id and
                                              // rejects its absence with 400.
}

// Update needs an id supplied as a separate argument; with none, it must fail
// early.
TEST_CASE("AppointmentController<ClinicAppointment>::Update with no id → 400",
          "[controllers][appointment]") {
  AppointmentController<ClinicAppointment>
       ctrl;  // A fresh clinic-appointment controller.
  auto r = capture([&](auto cb) {
    ctrl.Update(std::move(cb),
                Requester(1, "users"),
                R"({"date":"2026-01-01"})",
                std::nullopt);
  });  // std::nullopt means "no id"; the valid JSON body is irrelevant
       // because the id check fails first.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves a missing target id is
                                              // caught before any update SQL
                                              // runs.
}

// Delete is the one operation that signals a missing id with 406 (Not
// Acceptable) rather than 400.
TEST_CASE("AppointmentController<ClinicAppointment>::Delete with no id → 406",
          "[controllers][appointment]") {
  AppointmentController<ClinicAppointment>
       ctrl;  // A fresh clinic-appointment controller.
  auto r = capture([&](auto cb) {
    ctrl.Delete(std::move(cb), Requester(1, "users"), std::nullopt);
  });  // Call Delete with no target id (std::nullopt).
  CHECK(r.status ==
        api::v2::Http::Status::NOT_ACCEPTABLE);  // Pins down Delete's
                                                 // distinct 406 response for
                                                 // a missing id.
}

// Search rejects a body that is not JSON at all.
TEST_CASE(
    "AppointmentController<ClinicAppointment>::Search with malformed JSON → "
    "400",
    "[controllers][appointment]") {
  AppointmentController<ClinicAppointment>
       ctrl;  // A fresh clinic-appointment controller.
  auto r = capture([&](auto cb) {
    ctrl.Search(std::move(cb), Requester(1, "users"), "bad{");
  });  // Call Search with invalid JSON and capture the reply.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves Search validates the
                                              // JSON before building a query.
}

// Search also rejects JSON that parses but lacks the fields it needs.
TEST_CASE(
    "AppointmentController<ClinicAppointment>::Search with incomplete data → "
    "406",
    "[controllers][appointment]") {
  AppointmentController<ClinicAppointment>
       ctrl;  // A fresh clinic-appointment controller.
  auto r = capture([&](auto cb) {
    ctrl.Search(std::move(cb), Requester(1, "users"), R"({"foo":"bar"})");
  });  // Valid JSON, but no recognised search fields.
  CHECK(r.status ==
        api::v2::Http::Status::NOT_ACCEPTABLE);  // Proves incomplete search
                                                 // criteria are refused with
                                                 // 406.
}

// ── Pharmacy / Laboratory / RadiologyCenter ──────────────────────────────────

// One case per remaining appointment type proves they all share the same
// forwarding behaviour (and are compiled correctly), without repeating the full
// seven-case matrix.
TEST_CASE(
    "AppointmentController<PharmacyAppointment>::Create rejects malformed JSON",
    "[controllers][appointment]") {
  AppointmentController<PharmacyAppointment>
       ctrl;  // A controller instantiated for pharmacy appointments.
  auto r = capture([&](auto cb) {
    ctrl.Create(std::move(cb), Requester(1, "users"), "bad{");
  });  // Same bad-JSON Create call, now through the pharmacy
       // specialization.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves the pharmacy type
                                              // behaves identically to the
                                              // clinic type.
}

TEST_CASE(
    "AppointmentController<LaboratoryAppointment>::Delete with no id → 406",
    "[controllers][appointment]") {
  AppointmentController<LaboratoryAppointment>
       ctrl;  // A controller instantiated for laboratory appointments.
  auto r = capture([&](auto cb) {
    ctrl.Delete(std::move(cb), Requester(1, "users"), std::nullopt);
  });  // Delete with no id on the laboratory specialization.
  CHECK(r.status ==
        api::v2::Http::Status::NOT_ACCEPTABLE);  // Proves the Delete/id rule is
                                                 // shared by this type too.
}

TEST_CASE(
    "AppointmentController<RadiologyCenterAppointment>::Search with malformed "
    "JSON → 400",
    "[controllers][appointment]") {
  AppointmentController<RadiologyCenterAppointment>
       ctrl;  // A controller instantiated for radiology-center appointments.
  auto r = capture([&](auto cb) {
    ctrl.Search(std::move(cb), Requester(1, "users"), "bad{");
  });  // Search with bad JSON on the radiology specialization.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves this last appointment
                                              // type follows the same
                                              // validation path.
}
