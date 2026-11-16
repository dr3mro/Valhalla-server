/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file test_helper.cpp
 * @brief Checks the small response-building helpers that produce the server's
 * standard JSON replies.
 * @details The tests cover failure, error, success and "prepare" helpers. A
 * shared helper captures the response object that each function hands to its
 * callback, then each case asserts the HTTP status, the presence of the
 * message, and the shape of the JSON envelope (a Status field and a Message
 * field).
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Nearly every API reply goes through these helpers, so the JSON envelope they
 * build is a contract with every client. If a helper changed its status code or
 * dropped a field, clients would break in subtle ways. These tests keep that
 * shared response format stable.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 test cases and assertions:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Callbacks passed as functions (std::function):
 * https://en.cppreference.com/w/cpp/utility/functional/function
 * - Lambdas (small inline functions):
 * https://en.cppreference.com/w/cpp/language/lambda
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#include <drogon/HttpResponse.h>  // The response object the helpers produce.
// HTTP status codes such as k500InternalServerError.
#include <drogon/HttpTypes.h>

// Catch2's TEST_CASE, REQUIRE and CHECK macros.
#include <catch2/catch_test_macros.hpp>
#include <functional>  // std::function, used to pass callbacks around.
#include <memory>      // Smart pointers for the captured response.
#include <string>      // std::string for body text checks.

#include "api/v2/helper/helper.hpp"  // Helper, the class under test.

using Helper = api::v2::Helper;  // Shorter name for the helper class.

// Helpers centralize the JSON response envelope (a Status field plus a Message)
// and the status code each outcome maps to; these tests pin that contract.

namespace  // Anonymous namespace keeps the helper private to this file.
{
  /**
   * Runs `call` with a callback that captures the response Helper produced.
   * @brief Invokes a helper and returns the response it created.
   */
  drogon::HttpResponsePtr captureResponse(const std::function<void(
          std::function<void(const drogon::HttpResponsePtr&)>)>& call)
  {
    drogon::HttpResponsePtr
        captured;  // Will hold the response handed to the callback.
    call([&captured](const drogon::HttpResponsePtr& response)
        { captured = response; });  // Callback stores the response for us.
    return captured;  // Give the captured response back to the test.
  }
}  // namespace

TEST_CASE("Helper::failureResponse answers with 500 and a failure payload",
    "[api][helper]")  // Generic failure path.
{
  const auto response = captureResponse(
      [](auto&& callback)
      {
        Helper::failureResponse("boom", std::move(callback));
      });  // Build a failure response carrying the message "boom".

  REQUIRE(response != nullptr);  // A response must exist before we inspect it.
  CHECK(response->getStatusCode() ==
        // Failure maps to HTTP 500.
        drogon::HttpStatusCode::k500InternalServerError);
  CHECK(response->getBody().find("boom") !=
        std::string::npos);  // The message appears in the body.
  CHECK(response->getBody().find("\"Status\"") !=
        std::string::npos);  // The envelope contains a Status field.
  CHECK(response->contentTypeString().find("application/json") !=
        std::string::npos);  // It is labelled as JSON.
}

TEST_CASE("Helper::errorResponse answers with the requested code",
    "[api][helper]")  // Caller chooses the status code.
{
  const auto response = captureResponse(
      [](auto&& callback)
      {
        Helper::errorResponse(drogon::HttpStatusCode::k401Unauthorized,
            "unauthorized", std::move(callback));
      });  // Ask for a 401 with a message.

  REQUIRE(response != nullptr);  // A response must exist.
  CHECK(response->getStatusCode() ==
        drogon::HttpStatusCode::k401Unauthorized);  // The requested code is
                                                    // honoured exactly.
  CHECK(response->getBody().find("unauthorized") !=
        std::string::npos);  // The message is present in the body.
}

TEST_CASE("Helper::successResponse answers with 200 and a plain message",
    "[api][helper]")  // Success path.
{
  const auto response = captureResponse(
      [](auto&& callback)
      {
        Helper::successResponse("Logout success.", std::move(callback));
      });  // Build a success response.

  REQUIRE(response != nullptr);  // A response must exist.
  CHECK(response->getStatusCode() ==
        drogon::HttpStatusCode::k200OK);  // Success maps to HTTP 200.
  CHECK(response->getBody().find("Logout success.") !=
        std::string::npos);  // The message is present.
}

TEST_CASE("Helper::prepare renders the status and message payload",
    "[api][helper]")  // Low-level text builder.
{
  const auto text = Helper::prepare(
      "Failure", "something went wrong");  // Render the JSON text directly.

  CHECK(text.find("\"Message\" : \"something went wrong\"") !=
        std::string::npos);  // The Message field has the given text.
  CHECK(text.find("\"Status\" : \"Failure\"") !=
        std::string::npos);  // The Status field has the given value.
}
