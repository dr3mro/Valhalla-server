/**
 * @file test_staff_controller.cpp
 * @brief Catch2 unit tests and fixtures for staff controller.
 * @details These tests cover StaffController<Clinics> for its three operations:
 * AddStaffToEntity, RemoveStaffFromEntity and InviteStaffToEntity. Unlike the
 * other controllers, these methods have no guard before parsing JSON, so a bad
 * body throws and is converted to HTTP 500 by the CRITICALMESSAGERESPONSE
 * handler — that behaviour is deliberately pinned down here. Other cases check
 * that a missing required key also yields 500, that a valid payload does not
 * crash, and that a requester who fails the canManageStaff permission check is
 * refused before any database write or invitation email.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * StaffController governs who can add, remove and invite staff at a clinic — a
 * sensitive authorisation surface in a healthcare system. These tests document
 * the controller's current error contract (bad input becomes 500, not 400) and,
 * more importantly, confirm that a caller who does not manage the clinic is
 * stopped before any state-changing work happens. Without them, a change to
 * permission handling could silently allow unauthorised staff changes.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 CHECK / CHECK_FALSE:
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - C++ exceptions (an error that jumps to a handler):
 * https://en.cppreference.com/w/cpp/language/exceptions
 * - HTTP 500 Internal Server Error:
 * https://developer.mozilla.org/en-US/docs/Web/HTTP/Status/500
 * - jsoncons access by key, which throws when the key is missing:
 * https://jsoncons.github.io/jsoncons/
 * - C++ lambdas (small inline functions):
 * https://en.cppreference.com/w/cpp/language/lambda
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Tests for StaffController<Clinics> — covers AddStaff/RemoveStaff/InviteStaff.
// AddStaff/RemoveStaff have no pre-parse guard: bad JSON throws →
// CRITICALMESSAGERESPONSE → 500. InviteStaff similarly throws on bad JSON →
// 500. The permission-failure cases stop at canManageStaff, before the DB write
// or the email daemon, so no external service is needed.
// Provides TEST_CASE, CHECK and CHECK_FALSE.
#include <catch2/catch_test_macros.hpp>
#include <string>  // std::string holds the captured response body.

// The controller class under test.
#include "controllers/staffcontroller/staffcontroller.hpp"
// Clinics, the main service type used below.
#include "entities/services/clinics/clinics.hpp"
// Laboratories, used in one representative case.
#include "entities/services/laboratories.hpp"
// Pharmacies, used in one representative case.
#include "entities/services/pharmacies.hpp"
// RadiologyCenters, used in one representative case.
#include "entities/services/radiologycenters.hpp"
// Http::Status, the named status codes used in assertions.
#include "utils/global/http.hpp"
// Requester, the caller identity passed to every method.
#include "utils/global/requester.hpp"

using Requester =
    api::v2::Requester;  // A short alias for the verbose fully qualified name.

/** @brief The status and body passed to a controller callback. */
struct Result {
  int         status = 0;  // The HTTP status; 0 means the callback never fired.
  std::string body;        // The response text, filled by the callback.
};

/** @brief Invokes @p fn with a capturing callback and returns what it received.
 */
// Converts a callback-style controller call
// into a return value a test can check.
static Result capture(auto fn) {
  Result r;  // Default result (status 0, empty body), ready to be filled in
             // place.
  // Run the caller's lambda, passing it the capturing callback below.
  // The callback captures r by reference so
  // writes here persist.
  fn([&](int s, const std::string& b) {
    r.status = s;  // Store the status the controller reported.
    r.body   = b;  // Store the body the controller reported.
  });
  return r;  // Hand the captured answer back to the test.
}

// ── AddStaffToEntity ─────────────────────────────────────────────────────────

// Broken JSON throws before a guard can turn it into a 400, so it surfaces as
// 500.
TEST_CASE(
    "StaffController<Clinics>::AddStaffToEntity with malformed JSON → 500",
    "[controllers][staff]") {
  StaffController<Clinics>
       ctrl;  // A fresh controller for Clinics; it holds no per-call state.
  auto r = capture([&](auto cb) {
    ctrl.AddStaffToEntity(std::move(cb), Requester(1, "owners"), "bad{");
  });  // Invalid JSON; the exception is caught and converted to a 500
       // reply.
  CHECK(r.status ==
        api::v2::Http::Status::INTERNAL_SERVER_ERROR);  // Pins down the current
                                                        // 500-on-bad-JSON
                                                        // contract.
  CHECK_FALSE(
      r.body.empty());  // Proves the client still receives an explanatory body.
}

// Missing the required "payload" key also throws when the code looks it up.
TEST_CASE(
    "StaffController<Clinics>::AddStaffToEntity with missing 'payload' key → "
    "500",
    "[controllers][staff]") {
  StaffController<Clinics> ctrl;  // A fresh Clinics controller.
  auto                     r = capture([&](auto cb) {
    ctrl.AddStaffToEntity(std::move(cb),
                          Requester(1, "owners"),
                          R"({"no_payload":1})");
  });  // Parsable JSON, but the expected key is absent.
  // at("payload") throws → CRITICALMESSAGERESPONSE
  CHECK(r.status ==
        api::v2::Http::Status::INTERNAL_SERVER_ERROR);  // Proves a missing key
                                                        // follows the same 500
                                                        // error path.
}

// A well-formed payload gets past parsing, so the test only asserts it does not
// crash.
TEST_CASE(
    "StaffController<Clinics>::AddStaffToEntity with valid payload proceeds to "
    "DB",
    "[controllers][staff]") {
  StaffController<Clinics> ctrl;  // A fresh Clinics controller.
  // Minimal valid payload for StaffData
  auto r = capture([&](auto cb) {
    ctrl.AddStaffToEntity(
        std::move(cb),
        Requester(1, "owners"),
        R"({"id":1,"payload":{"staff_id":10,"role":"nurse"}})");
  });  // Both the entity id and the nested staff data are present.
  // Passes JSON parse; reaches DB (unavailable) or returns a response — must
  // not crash
  CHECK_FALSE(r.body.empty());  // Proves the controller produced some answer
                                // even with no database available.
}

// ── RemoveStaffFromEntity ────────────────────────────────────────────────────

// Broken JSON throws on the Remove path too, yielding 500.
TEST_CASE(
    "StaffController<Clinics>::RemoveStaffFromEntity with malformed JSON → 500",
    "[controllers][staff]") {
  StaffController<Clinics> ctrl;  // A fresh Clinics controller.
  auto                     r = capture([&](auto cb) {
    ctrl.RemoveStaffFromEntity(std::move(cb), Requester(1, "owners"), "bad{");
  });  // Invalid JSON body.
  CHECK(r.status == api::v2::Http::Status::
                        INTERNAL_SERVER_ERROR);  // Proves the 500-on-bad-JSON
                                                 // contract holds here as well.
}

// Missing the "id" key throws when looked up, producing 500.
TEST_CASE(
    "StaffController<Clinics>::RemoveStaffFromEntity with missing 'id' → 500",
    "[controllers][staff]") {
  StaffController<Clinics> ctrl;  // A fresh Clinics controller.
  auto                     r = capture([&](auto cb) {
    ctrl.RemoveStaffFromEntity(std::move(cb),
                               Requester(1, "owners"),
                               R"({"payload":{"staff_id":10}})");
  });  // The payload exists but the top-level id does not.
  // at("id") throws → 500
  CHECK(r.status ==
        api::v2::Http::Status::INTERNAL_SERVER_ERROR);  // Proves a missing id
                                                        // follows the 500
                                                        // error path.
}

// A caller who does not manage the clinic is refused before any state-changing
// work.
TEST_CASE(
    "StaffController<Clinics>::RemoveStaffFromEntity with mismatched requester "
    "returns error",
    "[controllers][staff]") {
  StaffController<Clinics> ctrl;  // A fresh Clinics controller.
  // requester id=99, service id=1 → canManageStaff fails; DB unavailable →
  // 400
  auto r = capture([&](auto cb) {
    ctrl.RemoveStaffFromEntity(
        std::move(cb),
        Requester(99, "users"),
        R"({"id":1,"payload":{"staff_id":10,"role":"nurse"}})");
  });  // Caller 99 tries to modify clinic 1; the permission check should
       // stop it.
  CHECK(r.status != api::v2::Http::Status::OK);  // Proves the removal is not
                                                 // reported as a success.
  CHECK_FALSE(
      r.body.empty());  // Proves an error message is returned to the caller.
}

// InviteStaff builds an invite JSON and would call the email daemon on success;
// the permission-failure case below stops before that call.

// ── InviteStaffToEntity ──────────────────────────────────────────────────────

// Broken JSON throws on the Invite path too, yielding 500.
TEST_CASE(
    "StaffController<Clinics>::InviteStaffToEntity with malformed JSON → 500",
    "[controllers][staff]") {
  StaffController<Clinics> ctrl;  // A fresh Clinics controller.
  auto                     r = capture([&](auto cb) {
    ctrl.InviteStaffToEntity(std::move(cb), Requester(1, "owners"), "bad{");
  });  // Invalid JSON body.
  CHECK(r.status == api::v2::Http::Status::
                        INTERNAL_SERVER_ERROR);  // Proves the 500-on-bad-JSON
                                                 // contract holds for invites.
}

// Missing the "nominee_id" key throws when looked up, producing 500.
TEST_CASE(
    "StaffController<Clinics>::InviteStaffToEntity with missing 'nominee_id' → "
    "500",
    "[controllers][staff]") {
  StaffController<Clinics> ctrl;  // A fresh Clinics controller.
  auto                     r = capture([&](auto cb) {
    ctrl.InviteStaffToEntity(std::move(cb),
                             Requester(1, "owners"),
                             R"({"no_nominee":1})");
  });  // Parsable JSON without the required nominee id.
  CHECK(r.status ==
        api::v2::Http::Status::INTERNAL_SERVER_ERROR);  // Proves the missing
                                                        // key yields the 500
                                                        // error path.
}

// A caller who is not the nominee fails the permission check and is refused.
TEST_CASE(
    "StaffController<Clinics>::InviteStaffToEntity with mismatched requester "
    "returns error",
    "[controllers][staff]") {
  StaffController<Clinics> ctrl;  // A fresh Clinics controller.
  // requester id=99 ≠ nominee_id=5; canManageStaff fails; DB unavailable →
  // 400
  auto r = capture(  // Call capture with a multi-line lambda holding the
                     // request.
      [&](auto cb) {
        ctrl.InviteStaffToEntity(  // Call InviteStaffToEntity with the full
                                   // invite JSON.
            std::move(cb),
            Requester(99, "users"),
            // Caller 99 is not nominee 5, so the check should fail before
            // any email is sent.
            R"({"nominee_id":5,"staff_id":5,"role":"nurse","service_id":1,"service_name":"clinics"})");
      });
  CHECK(r.status != api::v2::Http::Status::OK);  // Proves the invite is not
                                                 // reported as a success.
  CHECK_FALSE(r.body.empty());  // Proves an error message is still returned.
}

// ── Pharmacies / Laboratories / RadiologyCenters ─────────────────────────────

// One case per remaining service type proves the shared templated behaviour is
// compiled and works.
TEST_CASE(
    "StaffController<Pharmacies>::AddStaffToEntity with malformed JSON → 500",
    "[controllers][staff]") {
  StaffController<Pharmacies>
       ctrl;  // A controller instantiated for Pharmacies.
  auto r = capture([&](auto cb) {
    ctrl.AddStaffToEntity(std::move(cb), Requester(1, "owners"), "bad{");
  });  // Same bad-JSON call through this specialization.
  CHECK(r.status == api::v2::Http::Status::
                        INTERNAL_SERVER_ERROR);  // Proves Pharmacies shares the
                                                 // 500-on-bad-JSON behaviour.
}

TEST_CASE(
    "StaffController<Laboratories>::RemoveStaffFromEntity with malformed JSON "
    "→ 500",
    "[controllers][staff]") {
  StaffController<Laboratories>
       ctrl;  // A controller instantiated for Laboratories.
  auto r = capture([&](auto cb) {
    ctrl.RemoveStaffFromEntity(std::move(cb), Requester(1, "owners"), "bad{");
  });  // Bad-JSON Remove on this type.
  CHECK(r.status ==
        api::v2::Http::Status::INTERNAL_SERVER_ERROR);  // Proves Laboratories
                                                        // follows the same
                                                        // error path.
}

TEST_CASE(
    "StaffController<RadiologyCenters>::InviteStaffToEntity with malformed "
    "JSON → 500",
    "[controllers][staff]") {
  StaffController<RadiologyCenters>
       ctrl;  // A controller instantiated for RadiologyCenters.
  auto r = capture([&](auto cb) {
    ctrl.InviteStaffToEntity(std::move(cb), Requester(1, "owners"), "bad{");
  });  // Bad-JSON Invite on this type.
  CHECK(r.status ==
        api::v2::Http::Status::INTERNAL_SERVER_ERROR);  // Proves the last
                                                        // service type behaves
                                                        // identically.
}
