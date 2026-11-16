/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file test_entity_controller.cpp
 * @brief Catch2 unit tests and fixtures for entity controller.
 * @details These tests exercise all five EntityController<User> operations
 * (Create, Read, Update, Delete and Search) without a database. They focus on
 * the checks that happen before a query: JSON parsing, required fields and
 * caller permissions. Each case supplies bad or incomplete input and checks the
 * exact HTTP status the controller answers with — 400 for bad JSON or a missing
 * id on Update, 406 for a missing id on Delete or unusable Search parameters,
 * and 403 for a permission mismatch.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * EntityController is the common engine that ServiceController and
 * AppointmentController delegate to wholesale, and that ClientController
 * delegates to for Read, Delete and Search. A bug here therefore affects many
 * endpoints at once. Pinning down the validation and authorisation rules at
 * this single, central layer prevents a regression from leaking into every
 * controller built on top of it — critical for a healthcare API where record
 * access must be strictly controlled.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE and assertions:
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - C++ class templates: https://en.cppreference.com/w/cpp/language/templates
 * - std::optional (a value that may be empty):
 * https://en.cppreference.com/w/cpp/utility/optional
 * - C++ lambdas (small inline functions):
 * https://en.cppreference.com/w/cpp/language/lambda
 * - C++ raw string literals:
 * https://en.cppreference.com/w/cpp/language/string_literal
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Tests for EntityController<User> — covers all 5 methods
// (Create/Read/Update/Delete/Search). ServiceController and
// AppointmentController delegate every method here, and ClientController
// delegates Read, Delete and Search, so these cases cover those paths too
// (ClientController's Create/Update use their own password-worker path). The
// status split is intentional: Update with no id is 400, Delete with no id is
// 406, and Search with unusable params is 406. Valid input reaches the DB,
// which is unavailable in the unit-test binary, so those cases only assert a
// body.
// Provides TEST_CASE, CHECK and CHECK_FALSE.
#include <catch2/catch_test_macros.hpp>
#include <optional>  // std::optional / std::nullopt for "no id supplied" cases.
#include <string>    // std::string holds the captured response body.

// The controller class under test.
#include "controllers/entitycontroller/entitycontroller.hpp"
// User, the entity this controller is specialized for.
#include "entities/people/user.hpp"
// Http::Status, the named status codes used in assertions.
#include "utils/global/http.hpp"
// Requester, the caller identity passed to every method.
#include "utils/global/requester.hpp"

using Requester =
    api::v2::Requester;  // A short alias for the verbose fully qualified name.

/** @brief The status HTTP code and the body text a controller produced. */
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

// Create rejects a body it cannot parse before attempting to build or store an
// entity.
TEST_CASE(
    "EntityController::Create rejects malformed JSON", "[controllers][entity]")
{
  EntityController<User> ctrl;  // A fresh controller for the User entity; it
                                // holds no per-call state.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.Create(std::move(cb), Requester(1, "users"), "{{bad");
      });  // Invalid JSON; std::move passes the callback through without
           // copying.
  CHECK(
      r.status == api::v2::Http::Status::BAD_REQUEST);  // Proves malformed JSON
                                                        // is refused with 400.
}

// Read needs both "id" and "schema". The permission check runs before schema
// validation, so an id mismatch is 403 even when the schema is well formed.

// ── Read ────────────────────────────────────────────────────────────────────

// An unparsable Read body is rejected immediately.
TEST_CASE(
    "EntityController::Read rejects malformed JSON", "[controllers][entity]")
{
  EntityController<User> ctrl;  // A fresh User controller.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.Read(std::move(cb), Requester(1, "users"), "not-json");
      });  // Plain text instead of JSON.
  CHECK(
      r.status == api::v2::Http::Status::BAD_REQUEST);  // Proves Read validates
                                                        // JSON first.
}

// A valid object still needs an "id" field to know which record to read.
TEST_CASE(
    "EntityController::Read rejects body missing 'id'", "[controllers][entity]")
{
  EntityController<User> ctrl;  // A fresh User controller.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.Read(
            std::move(cb), Requester(1, "users"), R"({"schema":["name"]})");
      });  // A schema is present but the id is not.
  CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);  // Proves the missing
                                                          // id is caught.
}

// Likewise an "id" without a "schema" is incomplete.
TEST_CASE("EntityController::Read rejects body missing 'schema'",
    "[controllers][entity]")
{
  EntityController<User> ctrl;  // A fresh User controller.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.Read(std::move(cb), Requester(1, "users"), R"({"id":5})");
      });  // An id is present but no schema fields are requested.
  CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);  // Proves the missing
                                                          // schema is caught.
}

// A caller may only read a record whose id they own; a mismatch is forbidden.
TEST_CASE("EntityController::Read with mismatched requester → 403",
    "[controllers][entity]")
{
  EntityController<User> ctrl;  // A fresh User controller.
  // id=5 but requester id=99 → assert_group_id_match fails
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.Read(std::move(cb), Requester(99, "users"),
            R"({"id":5,"schema":["name"]})");
      });  // Both fields are valid, but caller 99 is not owner 5.
  CHECK(r.status ==
        api::v2::Http::Status::FORBIDDEN);  // Proves the permission check
                                            // blocks cross-account reads.
}

// ── Update ──────────────────────────────────────────────────────────────────

// Update cannot act without knowing which record to change (400, not 406).
TEST_CASE("EntityController::Update with no id → 400", "[controllers][entity]")
{
  EntityController<User> ctrl;  // A fresh User controller.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.Update(std::move(cb), Requester(1, "users"), R"({"name":"x"})",
            std::nullopt);
      });  // Valid JSON but no id argument.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Pins down Update's 400
                                              // response for a missing id.
}

// Even with an id, a malformed body is refused.
TEST_CASE("EntityController::Update with malformed JSON → 400",
    "[controllers][entity]")
{
  EntityController<User> ctrl;  // A fresh User controller.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.Update(std::move(cb), Requester(5, "users"), "{{bad",
            std::optional<uint64_t>{5});
      });  // Id 5 supplied, but the body is not JSON.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves body validation is not
                                              // skipped when an id is present.
}

// ── Delete ──────────────────────────────────────────────────────────────────

// Delete signals a missing id with 406, deliberately different from Update's
// 400.
TEST_CASE("EntityController::Delete with no id → 406", "[controllers][entity]")
{
  EntityController<User> ctrl;  // A fresh User controller.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.Delete(std::move(cb), Requester(1, "users"), std::nullopt);
      });  // No target id.
  CHECK(r.status ==
        api::v2::Http::Status::NOT_ACCEPTABLE);  // Pins down the 406 response
                                                 // for a missing id.
}

// A caller may only delete their own record.
TEST_CASE("EntityController::Delete with mismatched requester → 403",
    "[controllers][entity]")
{
  EntityController<User> ctrl;  // A fresh User controller.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.Delete(
            std::move(cb), Requester(99, "users"), std::optional<uint64_t>{5});
      });  // Caller 99 tries to delete record 5.
  CHECK(r.status ==
        api::v2::Http::Status::FORBIDDEN);  // Proves the permission check
                                            // blocks cross-account deletes.
}

// When the caller does match, the request gets as far as the (unavailable)
// database.
TEST_CASE(
    "EntityController::Delete with matching requester proceeds to DB "
    "(unavailable)",
    "[controllers][entity]")
{
  EntityController<User> ctrl;  // A fresh User controller.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.Delete(
            std::move(cb), Requester(5, "users"), std::optional<uint64_t>{5});
      });  // Caller 5 deletes record 5, so permissions pass and the DB is
           // reached.
  CHECK_FALSE(
      r.body.empty());  // Proves a response body is still produced rather
                        // than crashing when the DB is unavailable.
}

// Search builds its entity from the JSON and reports success separately from a
// thrown parse error: unusable params give 406, not a generic 500.

// ── Search ──────────────────────────────────────────────────────────────────

// Search refuses a body that is not JSON.
TEST_CASE("EntityController::Search with malformed JSON → 400",
    "[controllers][entity]")
{
  EntityController<User> ctrl;  // A fresh User controller.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.Search(std::move(cb), Requester(1, "users"), "not-json");
      });  // Plain text instead of JSON.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves unparsable input is
                                              // refused with 400.
}

// Search also refuses JSON that parses but lacks the fields it needs.
TEST_CASE("EntityController::Search with incomplete search data → 406",
    "[controllers][entity]")
{
  EntityController<User> ctrl;  // A fresh User controller.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.Search(std::move(cb), Requester(1, "users"), R"({"foo":"bar"})");
      });  // Valid JSON, but no recognised search fields.
  CHECK(r.status ==
        api::v2::Http::Status::NOT_ACCEPTABLE);  // Proves incomplete criteria
                                                 // yield 406.
}

// A complete search request gets as far as the (unavailable) database.
TEST_CASE(
    "EntityController::Search with valid search fields proceeds to DB "
    "(unavailable)",
    "[controllers][entity]")
{
  EntityController<User> ctrl;  // A fresh User controller.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.Search(std::move(cb), Requester(1, "users"),
            R"({"keyword":"alice","filter":"name","order_by":"id","direction":0,"limit":10,"offset":0})");
      });  // All required fields present, so parsing succeeds and the DB path
           // is reached.
  CHECK_FALSE(r.body.empty());  // Proves a body is returned even though the
                                // DB could not be contacted.
}
