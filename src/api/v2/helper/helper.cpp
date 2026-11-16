/**
 * @file helper.cpp
 * @brief Implementations of the shared v2 JSON response helpers.
 * @details This file contains the bodies of the Helper class functions declared
 * in helper.hpp. Each public function (failureResponse, errorResponse,
 * successResponse) delegates to the private reply function with the right
 * status and body. reply creates a Drogon response, sets the status, JSON
 * content type and body, adds Retry-After for 503, and calls the callback. The
 *          prepare function builds a JSON object with a "Status" tag plus the
 * message.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is where the API's response format is actually produced. Every JSON
 * success and error sent by controllers and filters passes through these few
 * functions, so this file guarantees that status codes, content types and
 * headers are set the same way everywhere. It is called by common.hpp, the Auth
 * filter and all route controllers. Without it there would be no central place
 * to enforce a consistent reply format or the retry hint for overloaded
 * servers.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - HTTP status codes: https://developer.mozilla.org/en-US/docs/Web/HTTP/Status
 * - The Retry-After header:
 * https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Retry-After
 * - std::move (transfer ownership instead of copying):
 * https://en.cppreference.com/w/cpp/utility/move
 * - CORS (background on HTTP headers):
 * https://developer.mozilla.org/en-US/docs/Web/HTTP/CORS
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */
// Own header: the declarations these functions implement.
#include "helper.hpp"

#include <drogon/HttpResponse.h>  // HttpResponse type created by reply().
#include <drogon/HttpTypes.h>     // Status codes and content-type constants.
// Json::Value used by prepare() to build the tagged object.
#include <json/value.h>

#include <functional>  // std::function callback type.
#include <string>      // std::string message and return types.
#include <utility>     // std::move for consuming the callback.

// JsonHelper::jsonify parses a message into JSON.
#include "utils/jsonhelper/jsonhelper.hpp"

using Helper = api::v2::Helper;  // Local alias so the definitions below can be
                                 // written concisely.

/**
 * @brief Build a 500 response whose body carries a "Failure" status tag.
 * @see Helper::failureResponse
 */
// Unexpected internal error -> status 500.
void Helper::failureResponse(
    const std::string&                                    message,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  reply(std::move(callback),
        drogon::k500InternalServerError,
        prepare(FAILURE,
                message));  // Wrap the message with a "Failure" tag and send it
                            // as 500.
}

/**
 * @brief Build a response with the given status and an "Error" status tag.
 * @see Helper::errorResponse
 */
// Expected error (400, 403, 503, ...).
void Helper::errorResponse(
    const drogon::HttpStatusCode&                         code,
    const std::string&                                    message,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  reply(std::move(callback),
        code,
        prepare(ERROR,
                message));  // Wrap the message with an "Error" tag and send it
                            // with the given code.
}

/**
 * @brief Build a 200 response sending the message through as-is (assumed
 * already JSON).
 * @see Helper::successResponse
 */
// Successful request -> status 200.
void Helper::successResponse(
    const std::string&                                    message,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  reply(std::move(callback),
        drogon::k200OK,
        message);  // Send the controller's JSON body unchanged (no Status tag).
}

/**
 * @brief Assemble the response, marking 503 with Retry-After: 1, and deliver it
 * via the callback.
 * @see Helper::reply
 */
// The one function that really builds and sends every reply.
void Helper::reply(
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const drogon::HttpStatusCode&                         code,
    const std::string&                                    message) {
  auto res = drogon::HttpResponse::newHttpResponse();  // Create an empty HTTP
                                                       // response object.
  res->setStatusCode(code);  // Set the HTTP status (200, 400, 500, ...).
  res->setContentTypeCode(
      drogon::CT_APPLICATION_JSON);  // Tell clients the body is JSON so they
                                     // parse it.
  res->setBody(message);             // Store the prepared body text.
  // A 503 means "temporarily
  // overloaded".
  if (code == drogon::k503ServiceUnavailable) {
    res->addHeader("Retry-After",
                   "1");  // Advise the client to retry after one second.
  }
  std::move(callback)(res);  // Deliver the response to Drogon; the callback
                             // is consumed exactly once.
}

/**
 * @brief Convert the message to a JSON value, add the "Status" field and
 * stringify it.
 * @see Helper::prepare
 */
std::string Helper::prepare(const std::string& status,
                            const std::string& message)  // NOLINT
{
  Json::Value object = JsonHelper::jsonify(
      message);  // Parse the message text into a JSON value.
  object["Status"] =
      status;  // Add the "Failure" or "Error" tag under the "Status" key.
  return object.toStyledString();  // Return a nicely formatted JSON string
                                   // for the response body.
}
