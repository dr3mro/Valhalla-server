/**
 * @file test_client_controller.cpp
 * @brief Catch2 unit tests for the pre-database behaviour of ClientController.
 * @details These tests cover ClientController<User> and
 * ClientController<Provider> without a real database. When
 * VALHALLA_TEST_NO_DB=1 every query returns "no value", so only the code that
 * runs before the first query is exercised: JSON parsing, required-field checks
 * and caller-permission checks. Each case sends deliberately bad or incomplete
 * data and checks the exact HTTP status (400, 403 or 406) the controller
 * answers with.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * ClientController backs the client/security endpoints (create, suspend,
 * activate, login, logout, CRUD, search and services for both users and
 * providers). These tests are the first line of defence for that surface: they
 * guarantee that malformed bodies never reach the database and that a caller
 * from the wrong group is refused with 403 before any record is touched. Losing
 * them would allow a validation or authorisation regression to ship silently,
 * which in a healthcare API could mean an unauthenticated or cross-account
 * action.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE and tags:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Catch2 CHECK / CHECK_FALSE:
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - C++ class templates: https://en.cppreference.com/w/cpp/language/templates
 * - std::optional (a value that may be empty):
 * https://en.cppreference.com/w/cpp/utility/optional
 * - std::move (transfer instead of copy):
 * https://en.cppreference.com/w/cpp/utility/move
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Tests for ClientController<User> and ClientController<Provider>.
// With VALHALLA_TEST_NO_DB=1 all DB queries return nullopt; every code path
// that runs before the first DB call is exercised hermetically.
// Provides TEST_CASE, CHECK and CHECK_FALSE.
#include <catch2/catch_test_macros.hpp>
// The callback type the controllers use is built on std::function.
#include <functional>
#include <optional>  // std::optional / std::nullopt for "no id supplied" cases.
#include <string>    // std::string holds the captured response body.

// The controller class under test.
#include "controllers/clientcontroller/clientcontroller.hpp"
// Provider, the second template argument exercised below.
#include "entities/people/provider.hpp"
// User, the main template argument exercised below.
#include "entities/people/user.hpp"
// Defines CALLBACK_, the "call me with (status, body)" function type.
#include "utils/global/callback.hpp"
// Http::Status, the named status codes used in assertions.
#include "utils/global/http.hpp"
// Requester, the identity passed to every method.
#include "utils/global/requester.hpp"

using Requester =
    api::v2::Requester;  // A short alias for the verbose fully qualified name.

/** @brief The status and body passed to a CALLBACK_ invocation. */
struct Result
{
  int status = 0;  // The HTTP status code; 0 means the callback never fired.
  std::string
      body;  // The response text; starts empty and is filled by the callback.
};

/** @brief Invokes @p fn with a capturing callback and returns what it received.
 */
static Result capture(auto fn)  // Converts a callback-style controller call
                                // into a return value a test can inspect.
{
  Result r;  // Default result (status 0, empty body), ready to be filled in
             // place.
  fn(  // Invoke the caller's lambda, passing it the capturing callback below.
      [&](int                s,
          const std::string& b)  // The callback captures r by reference so
                                 // writing to it here is visible outside.
      {
        r.status = s;  // Record the status code chosen by the controller.
        r.body   = b;  // Record the body produced by the controller.
      });
  return r;  // Give the captured answer back to the test.
}

// ── ClientController<User> ──────────────────────────────────────────────────

// Pre-database validation is the focus: malformed JSON yields 400, a missing id
// yields 400 (Update/GetServices) or 406 (Suspend/Activate/Delete), and a
// mismatched requester is rejected with 403 before any query runs.

// A broken JSON body is rejected before any user record is built or queried.
TEST_CASE("ClientController<User>::Create rejects malformed JSON",
          "[controllers][client]")
{
  ClientController<User> ctrl;  // A fresh controller for User clients; it
                                // keeps no per-call state.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.Create(std::move(cb), Requester(0, "users"), "not json{{");
      });  // Call Create with invalid JSON; the anonymous-style requester (id
           // 0) is fine because parsing fails first.
  CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);  // Proves bad JSON is
                                                          // refused with 400.
  CHECK_FALSE(r.body.empty());  // Proves the client also gets an explanatory
                                // message, not a blank reply.
}

// An empty body is simply invalid JSON, so it must be refused the same way.
TEST_CASE("ClientController<User>::Create rejects empty body",
          "[controllers][client]")
{
  ClientController<User> ctrl;  // A fresh User-client controller.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.Create(std::move(cb), Requester(0, "users"), "");
      });  // Pass an empty string as the body.
  // Empty string is invalid JSON → BAD_REQUEST
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves the empty-body edge case
                                              // is handled, not crashed on.
}

// Suspend needs a target id; none means the request cannot be acted on (406).
TEST_CASE("ClientController<User>::Suspend returns 406 for missing id",
          "[controllers][client]")
{
  ClientController<User> ctrl;  // A fresh User-client controller.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.Suspend(std::move(cb), Requester(1, "users"), std::nullopt);
      });  // std::nullopt means "no id supplied".
  CHECK(r.status ==
        api::v2::Http::Status::NOT_ACCEPTABLE);  // Proves a missing id is
                                                 // refused before any
                                                 // permission check or update.
}

// With an id present, the requester's group is checked; "admins" cannot act on
// the "users" table.
TEST_CASE(
    "ClientController<User>::Suspend with id calls permission check (group "
    "mismatch → 403)",
    "[controllers][client]")
{
  ClientController<User> ctrl;  // A fresh User-client controller.
  // Requester group "admins" does not match "users" table → FORBIDDEN
  auto r = capture(
      [&](auto cb)
      {
        ctrl.Suspend(std::move(cb),
                     Requester(42, "admins"),
                     std::optional<uint64_t>{42});
      });  // An id is supplied, so the permission check runs and should fail
           // on the group mismatch.
  CHECK(r.status ==
        api::v2::Http::Status::FORBIDDEN);  // Proves cross-group suspension
                                            // is blocked with 403.
}

// The same missing-id rule applies to Activate.
TEST_CASE("ClientController<User>::Activate returns 406 for missing id",
          "[controllers][client]")
{
  ClientController<User> ctrl;  // A fresh User-client controller.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.Activate(std::move(cb), Requester(1, "users"), std::nullopt);
      });  // Activate with no target id.
  CHECK(r.status ==
        api::v2::Http::Status::NOT_ACCEPTABLE);  // Proves Activate refuses a
                                                 // missing id with 406.
}

// Activate also checks the requester; a caller from "providers" cannot activate
// a user.
TEST_CASE(
    "ClientController<User>::Activate with id and mismatched requester → 403",
    "[controllers][client]")
{
  ClientController<User> ctrl;  // A fresh User-client controller.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.Activate(std::move(cb),
                      Requester(99, "providers"),
                      std::optional<uint64_t>{5});
      });  // Id 5 present, but the caller is in the wrong group.
  CHECK(r.status ==
        api::v2::Http::Status::FORBIDDEN);  // Proves the group check runs
                                            // before the database is touched.
}

// GetServices must reject a missing client id; the exact error is less
// important than "not success".
TEST_CASE("ClientController<User>::GetServices returns error for missing id",
          "[controllers][client]")
{
  ClientController<User> ctrl;  // A fresh User-client controller.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.GetServices(std::move(cb), Requester(1, "users"), std::nullopt);
      });  // GetServices with no id.
  // GetServices rejects a missing client_id up front with 400, before the
  // permission check and Controller::GetServices. The loose assertion only
  // pins down that no successful response is produced.
  CHECK(r.status != api::v2::Http::Status::OK);  // Proves the request does
                                                 // not pretend to succeed.
  CHECK_FALSE(r.body.empty());  // Proves an error message is returned
                                // alongside the non-OK status.
}

// A present id still requires a matching requester group for GetServices.
TEST_CASE("ClientController<User>::GetServices with mismatched requester → 403",
          "[controllers][client]")
{
  ClientController<User> ctrl;  // A fresh User-client controller.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.GetServices(std::move(cb),
                         Requester(10, "providers"),
                         std::optional<uint64_t>{10});
      });  // Id present but caller is in "providers", not "users".
  CHECK(r.status == api::v2::Http::Status::FORBIDDEN);  // Proves the permission
                                                        // gate blocks the call.
}

// Login is delegated to the gatekeeper; malformed credentials produce an error
// reply.
TEST_CASE(
    "ClientController<User>::Login delegates to gatekeeper (bad JSON → error)",
    "[controllers][client]")
{
  ClientController<User> ctrl;  // A fresh User-client controller.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.Login(std::move(cb),
                   Requester(0, "users"),
                   "bad json",
                   "127.0.0.1");
      });  // Pass broken JSON credentials and a client IP.
  CHECK(r.status != api::v2::Http::Status::OK);  // Proves no login is granted
                                                 // on unparsable input.
  CHECK_FALSE(
      r.body.empty());  // Proves the failure carries a message for the client.
}

// Logout with no token cannot identify a session, so the gatekeeper reports an
// error.
TEST_CASE(
    "ClientController<User>::Logout delegates to gatekeeper (empty token → "
    "error)",
    "[controllers][client]")
{
  ClientController<User> ctrl;  // A fresh User-client controller.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.Logout(std::move(cb),
                    Requester(0, "users"),
                    std::nullopt,
                    "127.0.0.1");
      });  // std::nullopt means "no token supplied".
  // No token → gatekeeper returns an error
  CHECK(r.status != api::v2::Http::Status::OK);  // Proves a token-less logout
                                                 // is not treated as a success.
}

// Update checks JSON validity before applying anything.
TEST_CASE("ClientController<User>::Update with malformed JSON → 400",
          "[controllers][client]")
{
  ClientController<User> ctrl;  // A fresh User-client controller.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.Update(std::move(cb),
                    Requester(5, "users"),
                    "{{bad",
                    std::optional<uint64_t>{5});
      });  // Bad JSON with an otherwise valid id.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves the body is validated
                                              // even when the id is present.
}

// Delete, like Suspend/Activate, refuses a missing id with 406.
TEST_CASE("ClientController<User>::Delete with no id → 406",
          "[controllers][client]")
{
  ClientController<User> ctrl;  // A fresh User-client controller.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.Delete(std::move(cb), Requester(1, "users"), std::nullopt);
      });  // Delete with no target id.
  CHECK(r.status ==
        api::v2::Http::Status::NOT_ACCEPTABLE);  // Pins down the 406 response
                                                 // for a missing id.
}

// Delete also enforces the group match before any deletion.
TEST_CASE("ClientController<User>::Delete with mismatched requester → 403",
          "[controllers][client]")
{
  ClientController<User> ctrl;  // A fresh User-client controller.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.Delete(std::move(cb),
                    Requester(99, "users"),
                    std::optional<uint64_t>{5});
      });  // Same group name but a different id (99 vs target 5) → mismatch.
  CHECK(r.status ==
        api::v2::Http::Status::FORBIDDEN);  // Proves id-level authorisation
                                            // blocks the delete.
}

// Read rejects bodies it cannot parse.
TEST_CASE("ClientController<User>::Read with malformed JSON → 400",
          "[controllers][client]")
{
  ClientController<User> ctrl;  // A fresh User-client controller.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.Read(std::move(cb), Requester(1, "users"), "{{bad");
      });  // Invalid JSON body for Read.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves Read validates
                                              // JSON first.
}

// Read also needs both an id and a schema; valid JSON without them is rejected.
TEST_CASE("ClientController<User>::Read with missing id → 400",
          "[controllers][client]")
{
  ClientController<User> ctrl;  // A fresh User-client controller.
  // Valid JSON but no "id" or "schema" field
  auto r = capture(
      [&](auto cb)
      {
        ctrl.Read(std::move(cb), Requester(1, "users"), R"({"foo":"bar"})");
      });  // Parses fine, but neither required field is present.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves required-field
                                              // validation runs after parsing.
}

// Search refuses a body that is not JSON.
TEST_CASE("ClientController<User>::Search with malformed JSON → 400",
          "[controllers][client]")
{
  ClientController<User> ctrl;  // A fresh User-client controller.
  auto                   r = capture(
      [&](auto cb)
      {
        ctrl.Search(std::move(cb), Requester(1, "users"), "not-json");
      });  // Plain text that is not JSON.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves Search rejects
                                              // unparsable input.
}

// Search needs its specific fields (keyword, filter, etc.); an unrelated object
// is refused.
TEST_CASE("ClientController<User>::Search with missing search fields → 406",
          "[controllers][client]")
{
  ClientController<User> ctrl;  // A fresh User-client controller.
  // Valid JSON but Search_t will fail to parse → NOT_ACCEPTABLE
  auto r = capture(
      [&](auto cb)
      {
        ctrl.Search(std::move(cb), Requester(1, "users"), R"({"foo":"bar"})");
      });  // Parses as JSON but cannot become a Search_t request.
  CHECK(r.status ==
        api::v2::Http::Status::NOT_ACCEPTABLE);  // Proves incomplete search
                                                 // criteria yield 406.
}

// ResetPassword is currently a no-op; the important behaviour is that it never
// crashes.
TEST_CASE("ClientController<User>::ResetPassword is a no-op (does not crash)",
          "[controllers][client]")
{
  ClientController<User> ctrl;  // A fresh User-client controller.
  bool called = false;  // A flag we flip inside the callback to see whether
                        // it is ever used.
  ctrl.ResetPassword(
      [&](int, const std::string&)
      {
        called = true;
      },
      Requester(1, "users"),
      R"({"x":1})");  // Supply a callback that would set the flag if invoked.
  // The implementation discards the callback — no crash is the expectation.
  CHECK_FALSE(called);  // Proves the callback is not called, documenting the
                        // current no-op behaviour.
}

// ── ClientController<Provider> ─────────────────────────────────────────────

// Provider shares the same templated implementation; these few cases recheck
// representative pre-database paths rather than repeating the whole User suite.

TEST_CASE("ClientController<Provider>::Create rejects malformed JSON",
          "[controllers][client][provider]")
{
  ClientController<Provider>
       ctrl;  // A controller instantiated for Provider clients.
  auto r = capture(
      [&](auto cb)
      {
        ctrl.Create(std::move(cb), Requester(0, "providers"), "bad{");
      });  // Same bad-JSON Create call through the Provider specialization.
  CHECK(r.status ==
        api::v2::Http::Status::BAD_REQUEST);  // Proves Provider parses input
                                              // the same way as User.
}

TEST_CASE("ClientController<Provider>::Suspend returns 406 for missing id",
          "[controllers][client][provider]")
{
  ClientController<Provider> ctrl;  // A fresh Provider-client controller.
  auto                       r = capture(
      [&](auto cb)
      {
        ctrl.Suspend(std::move(cb), Requester(1, "providers"), std::nullopt);
      });  // Suspend with no id.
  CHECK(r.status ==
        api::v2::Http::Status::NOT_ACCEPTABLE);  // Proves the missing-id rule
                                                 // holds for Providers too.
}

TEST_CASE(
    "ClientController<Provider>::GetServices returns error for missing id",
    "[controllers][client][provider]")
{
  ClientController<Provider> ctrl;  // A fresh Provider-client controller.
  auto                       r = capture(
      [&](auto cb)
      {
        ctrl.GetServices(std::move(cb),
                         Requester(1, "providers"),
                         std::nullopt);
      });                                        // GetServices with no id.
  CHECK(r.status != api::v2::Http::Status::OK);  // Proves no success is
                                                 // reported without an id.
  CHECK_FALSE(r.body.empty());  // Proves an error message is still returned
                                // to the caller.
}
