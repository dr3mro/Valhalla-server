/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file helper.hpp
 * @brief Factory for the standard JSON HTTP responses used by the v2
 * controllers and filters.
 * @details This header declares the Helper utility class. Its static functions
 * build the three kinds of reply the API sends: a 500 "Failure", an
 * arbitrary-status "Error", and a 200 "Success" whose body is already JSON. A
 * private reply function does the actual work of setting the status, content
 * type and body, and adds a Retry-After header to 503 replies. The prepare
 *          function wraps a message in a JSON object with a "Status" field.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Helper gives the entire API one consistent response format. Controllers and
 * filters call these functions instead of hand-building HTTP responses, so
 * clients always receive the same JSON shape for errors and the same headers
 * for retryable failures. It is used by common.hpp, the Auth filter and every
 * route controller. Without it each handler would invent its own error format
 * and clients could not reliably understand failures.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Static utility class: a class used as a namespace for functions, never
 * instantiated.
 * - HTTP status codes: https://developer.mozilla.org/en-US/docs/Web/HTTP/Status
 * - std::function callback:
 * https://en.cppreference.com/w/cpp/utility/functional/function
 * - JSON:
 * https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/JSON
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Only process this header once per source file.
#include <drogon/HttpResponse.h>  // Response object type built by the helpers.
// HTTP status code constants (k200OK, k500InternalServerError, ...).
#include <drogon/HttpTypes.h>
#include <drogon/drogon.h>  // Core Drogon definitions.
// Json::Value used to assemble the tagged error object.
#include <json/value.h>

#include <functional>  // std::function is the type of the response callback.
// JSON parsing/serialization library used by the implementation.
#include <jsoncons/json.hpp>
#include <string>  // std::string for messages and the returned JSON text.

namespace api::v2  // The v2 API helpers live here.
{

  /**
   * @class Helper
   * @brief Factory for the standard JSON HTTP responses used by the v2
   * controllers and filters.
   * @details All members are static; the class is a namespace-like utility
   * with no state. The response callbacks are consumed (moved from) exactly
   * once to deliver the reply. failure/error responses wrap the message in a
   * JSON object with a "Status" field, while success responses pass the
   * message through as-is. Every reply is JSON with a 503 response
   * additionally carrying Retry-After: 1.
   * @note Copyable but non-movable; construction is unrestricted but
   * meaningless since there is no instance state.
   */
  class Helper  // Pure utility class: all functions are static and it holds
                // no per-object data.
  {
   public:
    /** @name Construction
     *  Trivial: the utility has no state. Copying is allowed; moving is
     * deleted. */
    /**
     * @{
     */
    Helper() = default;  // Default constructor is fine; there is nothing to
                         // initialise.
    Helper(const Helper&) = default;  // Copying an empty object is harmless.
    Helper(
        Helper&&) = delete;  // Moving is disabled so the type stays copy-only.
    Helper& operator=(
        const Helper&)          = default;  // Copy assignment is also harmless.
    Helper& operator=(Helper&&) = delete;  // Move assignment disabled, matching
                                           // the deleted move constructor.
    virtual ~Helper() = default;           // Virtual destructor kept for
                                  // compatibility; no resources to free.
    /**
     * @}
     */

    /**
     * @brief Send a 500 response with a "Failure"-tagged JSON body.
     * @param[in] message Human-readable failure detail.
     * @param[in,out] callback Response callback, consumed exactly once.
     */
    static void failureResponse(const std::string& message,
        std::function<void(const drogon::HttpResponsePtr&)>&&
            callback);  // Used for unexpected internal errors.
    /**
     * @brief Send a response with an arbitrary status and an "Error"-tagged
     * JSON body.
     * @param[in] code HTTP status code to set.
     * @param[in] message Human-readable error detail.
     * @param[in,out] callback Response callback, consumed exactly once.
     */
    static void
    errorResponse(  // Used for expected errors such as 400, 403, 503.
        const drogon::HttpStatusCode& code, const std::string& message,
        std::function<void(const drogon::HttpResponsePtr&)>&&
            callback);  // code becomes the HTTP status; message is the body
                        // text.
    /**
     * @brief Send a 200 response whose body is the message verbatim
     * (already JSON).
     * @param[in] message JSON body text to send unchanged.
     * @param[in,out] callback Response callback, consumed exactly once.
     */
    static void        successResponse(const std::string& message,
        std::function<void(const drogon::HttpResponsePtr&)>&&
            callback);  // Success bodies come straight from controllers as
                               // JSON.
    /**
     * @brief Wrap a message in a JSON object with a "Status" field.
     * @param[in] status Value for the "Status" field (e.g. "Failure" or
     * "Error").
     * @param[in] message Message to store under the message key.
     * @return Pretty-printed JSON string.
     */
    static std::string prepare(const std::string& status,
        const std::string& message);  // Builds the tagged error body used
                                      // by failureResponse/errorResponse.

   private:
    /**
     * @brief Build and dispatch a JSON response; adds Retry-After on 503.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] code HTTP status code to set.
     * @param[in] message Body text to send.
     */
    static void reply(
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const drogon::HttpStatusCode&                         code,
        const std::string& message);  // Single place that actually creates
                                      // and sends every response.

    /**
     * Status tag used by failureResponse.
     */
    static constexpr const char* FAILURE =
        "Failure";  // String placed in the "Status" field of a 500 reply.
    /**
     * Status tag used by errorResponse.
     */
    static constexpr const char* ERROR =
        "Error";  // String placed in the "Status" field of other error
                  // replies.
  };
}  // namespace api::v2
