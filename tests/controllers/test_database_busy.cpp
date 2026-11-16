/**
 * @file test_database_busy.cpp
 * @brief Catch2 unit tests and fixtures for database busy.
 * @details These tests check how a busy database (a connection pool with no
 * free connection) is reported to the client. They verify three things in
 * sequence: the exception mapper turns DatabaseBusy into HTTP 503 but other
 * exceptions into 500; the response builder adds a Retry-After header only to
 * 503 replies; and a controller using a busy fake database surfaces 503 with
 * the exception text as the body. A final case makes sure a normal (non-busy)
 * database failure is NOT mislabelled as 503.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * When the database pool is exhausted the server must tell clients "try again
 * shortly" instead of hiding the problem as a generic 500. Retry-After lets
 * well-behaved clients back off instead of hammering the server. This file
 * protects that end-to-end classification across the mapper, the HTTP helper
 * and the controller's exception handler; a regression could cause retry storms
 * or silently mask overload as a broken query, which is especially risky for
 * appointment booking.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 CHECK vs REQUIRE (REQUIRE aborts the test on failure):
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - C++ exceptions and dynamic_cast:
 * https://en.cppreference.com/w/cpp/language/dynamic_cast
 * - HTTP 503 Service Unavailable:
 * https://developer.mozilla.org/en-US/docs/Web/HTTP/Status/503
 * - The Retry-After header:
 * https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Retry-After
 * - RAII guards: https://en.cppreference.com/w/cpp/language/raii
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// A pool timeout (DatabaseBusy) must reach the client as 503 with Retry-After,
// not as a generic failure. Two layers cooperate: Http::statusForException maps
// only DatabaseBusy to 503, and Helper::reply adds Retry-After only to 503
// responses. The controller cases confirm the CRITICALMESSAGERESPONSE macro
// preserves that classification.
// drogon::HttpResponse and HttpResponsePtr, used to inspect the built response.
#include <drogon/HttpResponse.h>
// Drogon's named status constants such as k503ServiceUnavailable.
#include <drogon/HttpTypes.h>

// Provides TEST_CASE, CHECK and REQUIRE.
#include <catch2/catch_test_macros.hpp>
// std::make_shared, used to create the shared fake database controller.
#include <memory>
// std::runtime_error, used as an example of an ordinary exception.
#include <stdexcept>
#include <string>  // std::string holds the response body.

// Helper::errorResponse / failureResponse, the response builders under test.
#include "api/v2/helper/helper.hpp"
// ClientController, used to test the controller-level classification.
#include "controllers/clientcontroller/clientcontroller.hpp"
// DatabaseBusy, the exception type raised by an exhausted pool.
#include "database/databasebusy.hpp"
// User, the entity the controller is specialized for.
#include "entities/people/user.hpp"
// FakeDatabaseController and FakeDbGuard, the hermetic DB stand-ins.
#include "support/fake_db.hpp"
// CALLBACK_, the callback type controllers answer through.
#include "utils/global/callback.hpp"
// statusForException, the mapper under test.
#include "utils/global/errorstatus.hpp"
// Http::Status, the named status codes used in assertions.
#include "utils/global/http.hpp"
// Requester, the caller identity passed to the controller.
#include "utils/global/requester.hpp"

namespace Http =
    api::v2::Http;  // A short alias so assertions read as Http::Status::OK.
using api::v2::Requester;  // Bring the requester type into this file's scope.

// The mapper must treat DatabaseBusy as the one special case deserving a
// retryable answer.
TEST_CASE(
    "statusForException maps DatabaseBusy to 503 and everything else to 500",
    "[database][busy]")
{
  CHECK(Http::statusForException(DatabaseBusy()) ==
        Http::Status::SERVICE_UNAVAILABLE);  // Proves a busy-pool exception
                                             // becomes 503 (retry later).
  CHECK(Http::statusForException(std::runtime_error("boom")) ==
        Http::Status::INTERNAL_SERVER_ERROR);  // Proves any other exception
                                               // still becomes 500 (generic
                                               // error).
}

// A 503 reply must tell the client how long to wait before trying again.
TEST_CASE("A 503 response carries Retry-After", "[database][busy]")
{
  drogon::HttpResponsePtr response;  // Starts empty; the helper will fill it
                                     // via the callback below.
  api::v2::Helper::errorResponse(drogon::k503ServiceUnavailable,
                                 "busy",
                                 [&response](const drogon::HttpResponsePtr& r)
                                 {
                                   response = r;
                                 });  // Build a 503 with message "busy" and
                                      // capture the produced response object.

  REQUIRE(response !=
          nullptr);  // Abort here if no response was produced; the following
                     // checks would otherwise dereference null.
  CHECK(response->statusCode() ==
        drogon::k503ServiceUnavailable);  // Proves the response really
                                          // carries status 503.
  CHECK(response->getHeader("Retry-After") ==
        "1");  // Proves the client is told to retry after one second.
}

// Ordinary failures must NOT carry Retry-After, otherwise clients would retry
// pointless requests.
TEST_CASE("Other error responses carry no Retry-After", "[database][busy]")
{
  drogon::HttpResponsePtr
      response;  // Starts empty; filled by the callback below.
  api::v2::Helper::failureResponse("boom",
                                   [&response](const drogon::HttpResponsePtr& r)
                                   {
                                     response = r;
                                   });  // Build a generic failure response with
                                        // message "boom".

  REQUIRE(response !=
          nullptr);  // Ensure a response exists before inspecting it.
  CHECK(response->statusCode() ==
        drogon::k500InternalServerError);  // Proves a generic failure maps to
                                           // 500.
  CHECK(response->getHeader("Retry-After")
            .empty());  // Proves no retry hint is attached to a 500.
}

// fake->busy makes every FakeDatabaseController method throw DatabaseBusy; the
// controller's generic exception handler must surface that as 503 with the
// exception text in the body.
TEST_CASE("A controller answers 503 when the database is busy",
          "[controller][client][busy]")
{
  auto fake =
      std::make_shared<FakeDatabaseController>();  // A shared fake so the
                                                   // guard and test see the
                                                   // same object.
  fake->busy = true;  // Switch the fake into "pool exhausted" mode: its
                      // queries now throw DatabaseBusy.

  FakeDbGuard guard(
      fake);  // Install the fake before the controller is constructed.
  ClientController<User>
      ctrl;          // The controller captures the fake from the Store.
  int code = -1;     // The captured status; -1 means the callback never fired.
  std::string body;  // The captured response body.

  ctrl.Activate(
      [&](int                c,
          const std::string& b)  // The callback captures code and body by
                                 // reference to fill them in place.
      {
        code = c;  // Record the status the controller chose.
        body = b;  // Record the body the controller produced.
      },
      Requester(42, "users"),
      42u);  // Caller 42 activates user 42; the busy fake will throw during
             // the query.

  CHECK(code ==
        Http::Status::SERVICE_UNAVAILABLE);  // Proves the controller
                                             // preserves the 503
                                             // classification end to end.
  CHECK(body.find("busy") !=
        std::string::npos);  // Proves the exception text reaches the client
                             // so the cause is visible.
}

// With busy=false, a nullopt DB result is the ordinary cruds failure (400).
// This case guards against over-broad classification of any DB failure as busy.
TEST_CASE("A non-busy database failure is still a 400/500, never 503",
          "[controller][client][busy]")
{
  auto fake = std::make_shared<FakeDatabaseController>();  // A fresh fake, not
                                                           // in busy mode.
  fake->query_result =
      std::nullopt;  // Arrange an ordinary failed write (no row returned).

  FakeDbGuard            guard(fake);  // Install the fake first.
  ClientController<User> ctrl;         // Controller captures it.
  int code = -1;  // The captured status; -1 means no callback fired.

  ctrl.Activate(
      [&](int c, const std::string&)
      {
        code = c;
      },
      Requester(42, "users"),
      42u);  // Same call as above, but the fake returns nothing instead of
             // throwing; the body is ignored here.

  CHECK(code !=
        Http::Status::SERVICE_UNAVAILABLE);  // Proves a normal DB failure is
                                             // not misclassified as "busy".
}
