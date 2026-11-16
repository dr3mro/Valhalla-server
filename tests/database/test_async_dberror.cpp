/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file test_async_dberror.cpp
 * @brief Catch2 unit tests and fixtures for async dberror. Checks how database
 * failures are translated into HTTP status codes.
 * @details The async layer has exactly one retryable failure: a busy connection
 * pool, which must become HTTP 503 (Service Unavailable) so callers know to try
 * again later. Every other error, such as an ordinary runtime error, must
 * become HTTP 500 (Internal Server Error). The tests also verify that isBusy()
 * recognises the typed DatabaseBusy exception while rejecting generic ones.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It protects asyncdberror.hpp, the error boundary that turns low-level DB
 * exceptions into the HTTP responses clients actually see. If a busy pool were
 * reported as 500, clients would treat a temporary overload as a permanent
 * failure and stop retrying; if ordinary bugs were reported as 503, clients
 * would retry pointlessly and real faults would be hidden. Correct mapping
 * keeps retry behaviour predictable.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE / SECTION:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Catch2 CHECK / REQUIRE assertion macros:
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Provides TEST_CASE and the CHECK / CHECK_FALSE assertion macros.
#include <catch2/catch_test_macros.hpp>
// std::runtime_error: a generic exception used as the "not busy" example.
#include <stdexcept>

// The production header under test: declares isBusy() and toHttpStatus().
#include "database/asyncdberror.hpp"
// Declares DatabaseBusy, the exception type that signals an exhausted DB pool.
#include "database/databasebusy.hpp"

// The async error boundary treats a busy pool as the only retryable failure
// (503); everything else is a 500. isBusy() also recognises Drogon errors by
// message text, but these cases cover the typed DatabaseBusy and a generic one.

TEST_CASE("error mapping maps DatabaseBusy to 503",
          "[database][async][error]")  // Registers the test for the one
                                       // retryable DB failure.
{
  DatabaseBusy busy;  // Construct the specific exception thrown when the
                      // connection pool has no free connection.
  CHECK(asyncdb::isBusy(busy));  // Prove it is classified as "busy", the
                                 // precondition for a retry hint.
  CHECK(asyncdb::toHttpStatus(busy) ==
        drogon::k503ServiceUnavailable);  // Prove it maps to HTTP 503, which
                                          // tells clients "try again later".
}

TEST_CASE("error mapping maps generic errors to 500",
          "[database][async][error]")  // Registers the test for every other,
                                       // non-retryable DB error.
{
  std::runtime_error generic(
      "boom");  // A plain exception with no special meaning, standing in for
                // an unexpected fault.
  CHECK_FALSE(
      asyncdb::isBusy(generic));  // Prove it is NOT classified as busy, so it
                                  // will not be retried as a transient issue.
  CHECK(asyncdb::toHttpStatus(generic) ==
        drogon::k500InternalServerError);  // Prove it maps to HTTP 500, the
                                           // generic "something went wrong"
                                           // response.
}
