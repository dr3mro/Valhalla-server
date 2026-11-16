/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file test_service_controller.cpp
 * @brief Catch2 unit tests and fixtures for service controller.
 * @details These tests check ServiceController<Clinics> across all five
 * CRUD-and-Search operations without a database. ServiceController forwards
 * everything to EntityController, so the cases really pin down the shared
 * pre-database validation: bad JSON gives 400, a missing id gives 400 on
 * Update/Read or 406 on Delete, and unusable search parameters give 406. One
 * representative case each for Pharmacies, Laboratories and RadiologyCenters
 * confirms the same templated behaviour holds for the other service types.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * ServiceController is the layer behind the service-management REST endpoints
 * (clinics, pharmacies, laboratories and radiology centres). These tests
 * protect the API contract for creating and editing those records, ensuring
 * malformed requests are rejected before any SQL runs and that Delete keeps its
 * distinct 406 answer. Because the controller is a thin template wrapper, a
 * change in forwarding or in the shared validator could otherwise go unnoticed
 * across all four service types at once.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE and assertions:
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - C++ class templates: https://en.cppreference.com/w/cpp/language/templates
 * - std::optional (a value that may be empty):
 * https://en.cppreference.com/w/cpp/utility/optional
 * - std::move (transfer instead of copy):
 * https://en.cppreference.com/w/cpp/utility/move
 * - C++ raw string literals:
 * https://en.cppreference.com/w/cpp/language/string_literal
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Tests for ServiceController<Clinics> — covers all 5 CRUD+Search paths.
// ServiceController forwards every method verbatim to EntityController, so the
// cases below exercise the shared pre-database validation (bad JSON → 400,
// missing id → 400/406, unusable search params → 406). A representative case
// per other service type checks the same templated behaviour.
#include <catch2/catch_test_macros.hpp>  // Provides TEST_CASE and CHECK.
#include <optional>  // std::optional / std::nullopt for "no id supplied" cases.
#include <string>    // std::string holds the captured response body.

// The controller class under test.
#include "controllers/servicecontroller/servicecontroller.hpp"
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
struct Result
{
  int         status = 0;  // The HTTP status; 0 means the callback never fired.
  std::string body;        // The response text, filled by the callback.
};

/** @brief Invokes @p fn with a capturing callback and returns what it received.
 */
static Result capture(auto fn)  // Converts a callback-style controller call
                                // into a return value a test can check.
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

// ── Create ──────────────────────────────────────────────────────────────────

// Create refuses a body it cannot parse before building or storing a service.
TEST_CASE("ServiceController<Clinics>::Create rejects malformed JSON",
          "[controllers][service]")
{
  ServiceController<Clinics>
       ctrl;  // A fresh controller for Clinics; it holds no per-call state.
  auto r = capture(
      [&](auto cb)
      {
        ctrl.Create(std::move(cb), Requester(1, "owners"), "bad{");
      });  // Invalid JSON; "owners" is the group allowed to manage services.
  CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);  // Proves bad JSON is
                                                          // refused with 400.
}

// ── Read ────────────────────────────────────────────────────────────────────

// Read rejects an unparsable body before any lookup.
TEST_CASE("ServiceController<Clinics>::Read rejects malformed JSON",
          "[controllers][service]")
{
  ServiceController<Clinics> ctrl;  // A fresh Clinics controller.
  auto                       r = capture(
      [&](auto cb)
      {
        ctrl.Read(std::move(cb), Requester(1, "owners"), "{{");
      });  // Invalid JSON body.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves Read validates
                                              // JSON first.
}

// A valid object without an id cannot identify which service to read.
TEST_CASE("ServiceController<Clinics>::Read with missing id → 400",
          "[controllers][service]")
{
  ServiceController<Clinics> ctrl;  // A fresh Clinics controller.
  auto                       r = capture(
      [&](auto cb)
      {
        ctrl.Read(std::move(cb),
                  Requester(1, "owners"),
                  R"({"schema":["name"]})");
      });  // A schema is present but no id.
  CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);  // Proves the missing
                                                          // id is caught.
}

// ── Update ──────────────────────────────────────────────────────────────────

// Update needs a target id; none means 400.
TEST_CASE("ServiceController<Clinics>::Update with no id → 400",
          "[controllers][service]")
{
  ServiceController<Clinics> ctrl;  // A fresh Clinics controller.
  auto                       r = capture(
      [&](auto cb)
      {
        ctrl.Update(std::move(cb),
                    Requester(1, "owners"),
                    R"({"name":"x"})",
                    std::nullopt);
      });  // Valid JSON but no id argument.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Pins down Update's
                                              // 400 for a missing id.
}

// Even with an id, Update still validates that the body is JSON.
TEST_CASE("ServiceController<Clinics>::Update with malformed JSON → 400",
          "[controllers][service]")
{
  ServiceController<Clinics> ctrl;  // A fresh Clinics controller.
  auto                       r = capture(
      [&](auto cb)
      {
        ctrl.Update(std::move(cb),
                    Requester(1, "owners"),
                    "bad{",
                    std::optional<uint64_t>{1});
      });  // Id 1 supplied, but the body is not JSON.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves body validation is not
                                              // skipped when an id is present.
}

// ── Delete ──────────────────────────────────────────────────────────────────

// Delete keeps its distinct 406 answer for a missing id.
TEST_CASE("ServiceController<Clinics>::Delete with no id → 406",
          "[controllers][service]")
{
  ServiceController<Clinics> ctrl;  // A fresh Clinics controller.
  auto                       r = capture(
      [&](auto cb)
      {
        ctrl.Delete(std::move(cb), Requester(1, "owners"), std::nullopt);
      });  // No target id.
  CHECK(r.status ==
        api::v2::Http::Status::NOT_ACCEPTABLE);  // Pins down the 406 response
                                                 // for a missing id.
}

// ── Search ──────────────────────────────────────────────────────────────────

// Search refuses a body that is not JSON.
TEST_CASE("ServiceController<Clinics>::Search with malformed JSON → 400",
          "[controllers][service]")
{
  ServiceController<Clinics> ctrl;  // A fresh Clinics controller.
  auto                       r = capture(
      [&](auto cb)
      {
        ctrl.Search(std::move(cb), Requester(1, "owners"), "bad{");
      });  // Invalid JSON.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves unparsable input is
                                              // refused with 400.
}

// Search also refuses JSON that parses but lacks the fields it needs.
TEST_CASE("ServiceController<Clinics>::Search with incomplete data → 406",
          "[controllers][service]")
{
  ServiceController<Clinics> ctrl;  // A fresh Clinics controller.
  auto                       r = capture(
      [&](auto cb)
      {
        ctrl.Search(std::move(cb), Requester(1, "owners"), R"({"x":1})");
      });  // Valid JSON with no recognised search fields.
  CHECK(r.status ==
        api::v2::Http::Status::NOT_ACCEPTABLE);  // Proves incomplete criteria
                                                 // yield 406.
}

// ── Pharmacies / Laboratories / RadiologyCenters ────────────────────────────

// One case per remaining service type proves the shared templated behaviour is
// compiled and works.
TEST_CASE("ServiceController<Pharmacies>::Create rejects malformed JSON",
          "[controllers][service]")
{
  ServiceController<Pharmacies>
       ctrl;  // A controller instantiated for Pharmacies.
  auto r = capture(
      [&](auto cb)
      {
        ctrl.Create(std::move(cb), Requester(1, "owners"), "bad{");
      });  // Same bad-JSON Create call through this specialization.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves Pharmacies validates
                                              // input the same way.
}

TEST_CASE("ServiceController<Laboratories>::Delete with no id → 406",
          "[controllers][service]")
{
  ServiceController<Laboratories>
       ctrl;  // A controller instantiated for Laboratories.
  auto r = capture(
      [&](auto cb)
      {
        ctrl.Delete(std::move(cb), Requester(1, "owners"), std::nullopt);
      });  // Delete with no id.
  CHECK(r.status ==
        api::v2::Http::Status::NOT_ACCEPTABLE);  // Proves the Delete/id rule
                                                 // holds for this type too.
}

TEST_CASE(
    "ServiceController<RadiologyCenters>::Search with malformed JSON → 400",
    "[controllers][service]")
{
  ServiceController<RadiologyCenters>
       ctrl;  // A controller instantiated for RadiologyCenters.
  auto r = capture(
      [&](auto cb)
      {
        ctrl.Search(std::move(cb), Requester(1, "owners"), "bad{");
      });  // Search with bad JSON.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves this last service type
                                              // follows the same validation
                                              // path.
}
