/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file test_client_controller_db.cpp
 * @brief Catch2 unit tests and fixtures for client controller db.
 * @details These tests drive ClientController<User> down to its database calls
 * using a fake DatabaseController instead of PostgreSQL. By setting the fake's
 * result fields, each case decides whether the "DB" returns nothing, an empty
 * array, or real rows, and then checks the status code and body the controller
 * produces. This covers Suspend, Activate, GetServices, Delete and Search,
 * including the difference between a failed query (400) and a successful one
 * (200).
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the bridge between pure validation tests and full integration tests.
 * It proves the controller maps database outcomes to the right HTTP answers
 * without needing a live server, so a regression in that mapping is caught
 * quickly. In a healthcare API these paths suspend accounts, delete clients and
 * list services; getting the status wrong could hide a failed operation or
 * expose data, and these tests keep that behaviour pinned down.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE and assertions:
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - RAII (a guard that cleans up when it goes out of scope):
 * https://en.cppreference.com/w/cpp/language/raii
 * - std::shared_ptr / std::make_shared:
 * https://en.cppreference.com/w/cpp/memory/shared_ptr/make_shared
 * - std::optional (a value that may be empty):
 * https://en.cppreference.com/w/cpp/utility/optional
 * - jsoncons JSON values: https://jsoncons.github.io/jsoncons/
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#include <catch2/catch_test_macros.hpp>  // Provides TEST_CASE and CHECK.
// jsoncons::json and json::array, used to build fake DB results.
#include <jsoncons/json.hpp>
// std::make_shared, used to create the shared fake database controller.
#include <memory>
// std::nullopt, used to say "no value came back from the DB".
#include <optional>
#include <string>  // std::string holds the captured response body.

// The controller class under test.
#include "controllers/clientcontroller/clientcontroller.hpp"
// User, the entity this controller is specialized for.
#include "entities/people/user.hpp"
// FakeDatabaseController and FakeDbGuard, the hermetic DB stand-ins.
#include "support/fake_db.hpp"
// CALLBACK_, the callback type controllers answer through.
#include "utils/global/callback.hpp"
// Http::Status, the named status codes used in assertions.
#include "utils/global/http.hpp"
// Requester, the caller identity passed to each method.
#include "utils/global/requester.hpp"

using api::v2::Requester;  // Bring the requester type into this file's scope.
namespace Http =
    api::v2::Http;  // A short alias so assertions read as Http::Status::OK.

// ── helpers ──────────────────────────────────────────────────────────────────

namespace  // An anonymous namespace keeps these helpers private to this test
           // file.
{
  /** @brief The status code and body passed to a controller callback. */
  struct CallResult
  {
    int code = -1;     // The HTTP status; -1 means the callback never fired.
    std::string body;  // The response text, filled by the callback.
  };

  /** @brief Builds a callback that records its code/body into @p out. */
  CALLBACK_ capture(CallResult& out)  // Returns a "call me later" function that
                                      // writes into out.
  {
    return [&out](int code,
                  const std::string&
                      body)  // The lambda captures out by reference so
                             // the result is visible to the caller.
    {
      out.code = code;  // Store the status the controller reported.
      out.body = body;  // Store the body the controller reported.
    };
  }

  /** @brief A requester that owns user id=42 in group "users". */
  Requester userRequester()
  {
    return Requester(42, "users");
  }  // A convenience factory for the happy-path caller identity.

  /** @brief A non-empty JSON result that makes Controller::cruds report
   * success (200). */
  jsoncons::json makeOkResult()  // Builds a minimal JSON object that counts
                                 // as a successful DB row.
  {
    jsoncons::json j =
        jsoncons::json::object();  // Start with an empty JSON object.
    j["id"] = 42;  // Add one field so the object is non-empty; cruds treats
                   // that as success.
    return j;      // Hand the ready-made result back to the test.
  }
}  // namespace

// FakeDbGuard must outlive the controller: Controller captures the fake from
// Store at construction, so it is installed before each controller is created.

// ── ClientController<User>::Create ───────────────────────────────────────────

// A broken Create body is rejected before any schema check or SQL, so no fake
// is needed here.
TEST_CASE("ClientController::Create rejects malformed JSON",
          "[controller][client][create]")
{
  ClientController<User> ctrl;  // A fresh User-client controller; no DB fake
                                // needed because parsing fails first.
  CallResult result;  // The envelope that will hold the callback's answer.
  ctrl.Create(capture(result),
              userRequester(),
              "not json {{{");  // Call Create with invalid JSON; the requester
                                // is the owner id=42.
  CHECK(result.code ==
        Http::Status::BAD_REQUEST);  // Proves bad JSON is refused with 400
                                     // before any DB work.
}

// NOTE: ClientController::Create with valid JSON is not covered by this DB-free
// fixture; those paths are exercised by the integration tests against a real
// database. The former justification (a UB end-iterator dereference in
// validateDatabaseCreateSchema, "known bug 3") is stale: the validator now
// checks for a missing table and returns early.

// Suspend and Activate share one contract: nullopt id → 406; a nullopt DB
// result → 400 via Controller::cruds; a non-empty result → 200.

// ── ClientController<User>::Suspend ──────────────────────────────────────────

// No id means there is nothing to suspend; the request is refused immediately.
TEST_CASE("ClientController::Suspend rejects nullopt id",
          "[controller][client][suspend]")
{
  ClientController<User> ctrl;    // A fresh User-client controller.
  CallResult             result;  // Holds the answer.
  ctrl.Suspend(capture(result),
               userRequester(),
               std::nullopt);                          // Pass no target id.
  CHECK(result.code == Http::Status::NOT_ACCEPTABLE);  // Proves the missing-id
                                                       // guard returns 406.
}

// With the fake returning no row, the shared CRUD helper reports a failed
// update as 400.
TEST_CASE("ClientController::Suspend returns 400 when DB returns nullopt",
          "[controller][client][suspend]")
{
  auto fake =
      std::make_shared<FakeDatabaseController>();  // Make a shared fake so
                                                   // the guard and test refer
                                                   // to the same object.
  fake->query_result = std::nullopt;  // executeQuery returns nothing

  FakeDbGuard guard(fake);  // Install the fake into the Store for the
                            // lifetime of this test.
  // Controller is constructed AFTER guard so it captures the fake.
  ClientController<User> ctrl;    // The controller looks up the fake from the
                                  // Store at construction time.
  CallResult             result;  // Holds the answer.

  ctrl.Suspend(capture(result),
               Requester(42, "users"),
               42u);  // Owner (42) suspends user 42; 42u is an unsigned id.
  // No result from DB → 400
  CHECK(result.code ==
        Http::Status::BAD_REQUEST);  // Proves a failed DB write is surfaced
                                     // as 400, not a false success.
}

// When the fake returns a row, the same call is a success (200).
TEST_CASE("ClientController::Suspend returns 200 when DB succeeds",
          "[controller][client][suspend]")
{
  // A fresh fake for this case.
  auto fake = std::make_shared<FakeDatabaseController>();
  fake->query_result =
      makeOkResult();  // Arrange for executeQuery to return a non-empty row.

  FakeDbGuard guard(fake);  // Install the fake before the controller is built.
  ClientController<User> ctrl;  // Controller captures the fake from the Store.
  CallResult             result;  // Holds the answer.

  ctrl.Suspend(capture(result),
               Requester(42, "users"),
               42u);  // Perform the suspend.
  CHECK(result.code ==
        Http::Status::OK);  // Proves a successful DB write is reported as 200.
}

// ── ClientController<User>::Activate ─────────────────────────────────────────

// Activate mirrors Suspend: a missing id is refused with 406.
TEST_CASE("ClientController::Activate rejects nullopt id",
          "[controller][client][activate]")
{
  ClientController<User> ctrl;    // A fresh User-client controller.
  CallResult             result;  // Holds the answer.
  ctrl.Activate(capture(result),
                userRequester(),
                std::nullopt);                         // No target id.
  CHECK(result.code == Http::Status::NOT_ACCEPTABLE);  // Proves the missing-id
                                                       // guard returns 406.
}

// A DB that returns no row makes Activate report 400.
TEST_CASE("ClientController::Activate returns 400 when DB returns nullopt",
          "[controller][client][activate]")
{
  // A fresh fake.
  auto fake          = std::make_shared<FakeDatabaseController>();
  fake->query_result = std::nullopt;  // Arrange a failed write.

  FakeDbGuard            guard(fake);  // Install the fake first.
  ClientController<User> ctrl;         // Controller captures it.
  CallResult             result;       // Holds the answer.

  ctrl.Activate(capture(result),
                Requester(42, "users"),
                42u);  // Perform the activate.
  CHECK(result.code ==
        Http::Status::BAD_REQUEST);  // Proves a nullopt DB result becomes 400.
}

// A DB row makes Activate report 200.
TEST_CASE("ClientController::Activate returns 200 when DB succeeds",
          "[controller][client][activate]")
{
  // A fresh fake.
  auto fake          = std::make_shared<FakeDatabaseController>();
  fake->query_result = makeOkResult();  // Arrange a successful write.

  FakeDbGuard            guard(fake);  // Install the fake first.
  ClientController<User> ctrl;         // Controller captures it.
  CallResult             result;       // Holds the answer.

  ctrl.Activate(capture(result),
                Requester(42, "users"),
                42u);  // Perform the activate.
  CHECK(result.code ==
        Http::Status::OK);  // Proves the success path returns 200.
}

// GetServices uses the search path: an empty array is still a 200 with "[]",
// while rows returned by the DB are forwarded as the response body.

// ── ClientController<User>::GetServices ──────────────────────────────────────

// A missing id is rejected before any permission check or query.
TEST_CASE("ClientController::GetServices rejects nullopt id",
          "[controller][client][services]")
{
  ClientController<User> ctrl;    // A fresh User-client controller.
  CallResult             result;  // Holds the answer.
  ctrl.GetServices(capture(result),
                   userRequester(),
                   std::nullopt);  // No client id supplied.
  CHECK(result.code ==
        Http::Status::BAD_REQUEST);  // Proves a missing id is refused with
                                     // 400 on this path.
}

// An empty search result is a valid answer: 200 with an empty JSON array, not
// an error.
TEST_CASE(
    "ClientController::GetServices returns empty array when DB has no results",
    "[controller][client][services]")
{
  // A fresh fake.
  auto fake           = std::make_shared<FakeDatabaseController>();
  fake->search_result = jsoncons::json::array{};  // empty array, not nullopt

  FakeDbGuard guard(fake);        // Install the fake before the controller.
  ClientController<User> ctrl;    // Controller captures it.
  CallResult             result;  // Holds the answer.

  ctrl.GetServices(capture(result),
                   Requester(42, "users"),
                   42u);                   // Ask for this client's services.
  CHECK(result.code == Http::Status::OK);  // Proves "no services" is still a
                                           // successful request.
  CHECK(result.body == "[]");  // Proves the body is exactly an empty JSON
                               // array, which clients can parse safely.
}

// Real service rows from the DB are serialised into the response body.
TEST_CASE("ClientController::GetServices returns service array from DB",
          "[controller][client][services]")
{
  jsoncons::json::array services;  // The list of rows the fake will return.
  jsoncons::json        svc = jsoncons::json::object();  // One service object.
  svc["id"]                 = 1;                         // Give it an id field.
  svc["name"] = "City Clinic";  // Give it a recognisable name to search for
                                // in the body.
  services.push_back(svc);      // Add the row to the result list.

  // A fresh fake.
  auto fake = std::make_shared<FakeDatabaseController>();
  fake->search_result =
      services;  // Arrange for executeSearchQuery to return our row.

  FakeDbGuard            guard(fake);  // Install the fake first.
  ClientController<User> ctrl;         // Controller captures it.
  CallResult             result;       // Holds the answer.

  ctrl.GetServices(capture(result),
                   Requester(42, "users"),
                   42u);                   // Ask for the services.
  CHECK(result.code == Http::Status::OK);  // Proves the request succeeded.
  CHECK(result.body.find("City Clinic") !=
        std::string::npos);  // Proves the DB data actually reached the
                             // response body.
}

// ── ClientController<User>::Delete ───────────────────────────────────────────

// Delete refuses a missing id before doing anything else.
TEST_CASE("ClientController::Delete rejects nullopt id",
          "[controller][client][delete]")
{
  ClientController<User> ctrl;    // A fresh User-client controller.
  CallResult             result;  // Holds the answer.
  ctrl.Delete(capture(result),
              userRequester(),
              std::nullopt);  // No id to delete.
  CHECK(result.code ==
        Http::Status::NOT_ACCEPTABLE);  // Proves the 406 missing-id rule
                                        // applies here too.
}

// A caller from the wrong group must not delete a user.
TEST_CASE("ClientController::Delete rejects mismatched requester group",
          "[controller][client][delete]")
{
  // requester is in "providers" but entity is a User ("users")
  ClientController<User> ctrl;    // A fresh User-client controller.
  CallResult             result;  // Holds the answer.
  ctrl.Delete(capture(result),
              Requester(42, "providers"),
              42u);  // Id matches but the group does not.
  CHECK(result.code == Http::Status::FORBIDDEN);  // Proves authorisation blocks
                                                  // the delete with 403.
}

// A failed delete at the DB layer is reported as 400.
TEST_CASE("ClientController::Delete returns 400 when DB returns nullopt",
          "[controller][client][delete]")
{
  // A fresh fake.
  auto fake          = std::make_shared<FakeDatabaseController>();
  fake->query_result = std::nullopt;  // Arrange a failed write.

  FakeDbGuard            guard(fake);  // Install the fake first.
  ClientController<User> ctrl;         // Controller captures it.
  CallResult             result;       // Holds the answer.

  ctrl.Delete(capture(result),
              Requester(42, "users"),
              42u);  // Perform the delete.
  CHECK(result.code ==
        Http::Status::BAD_REQUEST);  // Proves a nullopt DB result becomes 400.
}

// A successful delete at the DB layer is reported as 200.
TEST_CASE("ClientController::Delete returns 200 when DB succeeds",
          "[controller][client][delete]")
{
  // A fresh fake.
  auto fake          = std::make_shared<FakeDatabaseController>();
  fake->query_result = makeOkResult();  // Arrange a successful write.

  FakeDbGuard            guard(fake);  // Install the fake first.
  ClientController<User> ctrl;         // Controller captures it.
  CallResult             result;       // Holds the answer.

  ctrl.Delete(capture(result),
              Requester(42, "users"),
              42u);  // Perform the delete.
  CHECK(result.code ==
        Http::Status::OK);  // Proves the success path returns 200.
}

// Search has three outcomes: bad JSON → 400, incomplete params → 406, otherwise
// 200 with the DB rows. The two-row case with limit=1 exercises the paging
// branch in Controller::Search, which discards the extra row.

// ── ClientController<User>::Search ───────────────────────────────────────────

// A non-JSON body is rejected before any query is built.
TEST_CASE("ClientController::Search rejects malformed JSON",
          "[controller][client][search]")
{
  ClientController<User> ctrl;    // A fresh User-client controller.
  CallResult             result;  // Holds the answer.
  ctrl.Search(capture(result),
              userRequester(),
              "bad json");  // Plain text instead of JSON.
  CHECK(result.code ==
        Http::Status::BAD_REQUEST);  // Proves Search refuses unparsable input.
}

// Search needs all its fields; leaving out "offset" makes the request
// unacceptable.
TEST_CASE("ClientController::Search rejects incomplete search params",
          "[controller][client][search]")
{
  // Missing "offset" field
  constexpr const char* incomplete =
      // A raw string literal so the JSON quotes stay literal; every field
      // except offset is present.
      R"({"keyword":"x","filter":"name","order_by":"id","direction":0,"limit":10})";
  ClientController<User> ctrl;    // A fresh User-client controller.
  CallResult             result;  // Holds the answer.
  ctrl.Search(capture(result),
              userRequester(),
              incomplete);  // Search with the incomplete JSON.
  CHECK(result.code ==
        Http::Status::NOT_ACCEPTABLE);  // Proves the missing field
                                        // is caught with 406.
}

// Returned rows are serialised into the body; the extra row also exercises
// paging logic.
TEST_CASE("ClientController::Search returns results from DB",
          "[controller][client][search]")
{
  jsoncons::json::array rows;  // The list of rows the fake will return.
  jsoncons::json row = jsoncons::json::object();  // One reusable row object.
  row["id"]          = 1;                         // Give the row an id.
  row["username"] = "alice";  // A recognisable value to look for in the body.
  rows.push_back(row);        // Add the first row.
  // Extra row to trigger the "more results" logic (limit=1 but 2 rows
  // returned)
  rows.push_back(row);  // Add a second identical row; with limit=1 this
                        // drives the paging branch.

  // A fresh fake.
  auto fake           = std::make_shared<FakeDatabaseController>();
  fake->search_result = rows;  // Arrange the fake's search result.

  FakeDbGuard            guard(fake);  // Install the fake first.
  ClientController<User> ctrl;         // Controller captures it.
  CallResult             result;       // Holds the answer.

  constexpr const char* search =
      // A complete search request asking for one row from offset zero.
      R"({"keyword":"alice","filter":"username","order_by":"id","direction":0,"limit":1,"offset":0})";
  ctrl.Search(capture(result), userRequester(), search);  // Run the search.
  CHECK(result.code ==
        Http::Status::OK);  // Proves a valid search with rows succeeds.
  CHECK(result.body.find("alice") !=
        std::string::npos);  // Proves the DB data is present in the returned
                             // body.
}

// Even with no rows, a valid search succeeds (200) and answers with an empty
// result set.
TEST_CASE("ClientController::Search returns empty results when DB has no rows",
          "[controller][client][search]")
{
  // A fresh fake.
  auto fake           = std::make_shared<FakeDatabaseController>();
  fake->search_result = jsoncons::json::array{};  // empty array

  FakeDbGuard            guard(fake);  // Install the fake first.
  ClientController<User> ctrl;         // Controller captures it.
  CallResult             result;       // Holds the answer.

  constexpr const char* search =
      // A complete search that will match nothing.
      R"({"keyword":"nobody","filter":"username","order_by":"id","direction":0,"limit":10,"offset":0})";
  ctrl.Search(capture(result), userRequester(), search);  // Run the search.
  CHECK(result.code ==
        Http::Status::OK);  // Proves "no matches" is a success, not an error.
}
