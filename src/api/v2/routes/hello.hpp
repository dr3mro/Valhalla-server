/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file hello.hpp
 * @brief Minimal welcome endpoint used to confirm the API is reachable.
 * @details This file defines the Hello controller with one tiny endpoint. When
 * called it always sends back the same JSON greeting with status 200. It reads
 * nothing from the request, touches no database and keeps no state, which makes
 * it the simplest possible example of a Drogon route. Its main job is to act as
 * a liveness check: if this endpoint answers, the server is up and the HTTP
 * stack is working.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the "is the API alive?" endpoint. Operators and automated health
 * checks call it to confirm the server is running without needing a login or
 * any data. Drogon serves it at /api/v2/hello through the INSECURE chain, so it
 * is rate-limited and timed but requires no token. It calls JsonHelper to build
 * the greeting; without it there would be no quick way to verify basic
 * connectivity.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Health/liveness endpoint: a trivial URL used to check a service is running.
 * - HTTP GET: https://developer.mozilla.org/en-US/docs/Web/HTTP/Methods
 * - HTTP filters/middleware: https://drogon.docsforge.com/master/filter/
 * - CORS: https://developer.mozilla.org/en-US/docs/Web/HTTP/CORS
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Only process this header once per source file.
// Drogon controller base and the request/response/status types used by the
// handler.
// Base class drogon::HttpController used by Hello.
#include <drogon/HttpController.h>
// Request type (unused here but required by the handler signature).
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>  // Response type built by the handler.
#include <drogon/HttpTypes.h>     // Status and content-type constants.

#include <functional>  // std::function is the type of the response callback.
#include <utility>     // std::move for consuming the callback.

// Brings in the INSECURE filter macro used by METHOD_ADD.
#include "api/v2/basic/common.hpp"
// JsonHelper::jsonify builds the JSON greeting.
#include "utils/jsonhelper/jsonhelper.hpp"
namespace api::v2  // The v2 API controllers live here.
{
  /**
   * @class Hello
   * @brief Minimal welcome endpoint used to confirm the API is reachable.
   * @details GET /api/v2/hello (INSECURE). Stateless: always returns the same
   * JSON greeting.
   */
  class Hello
      : public drogon::HttpController<Hello>  // Inherit Drogon's controller
                                              // template so Drogon registers
                                              // its routes.
  {
   public:
    /**
     * @brief Respond with a fixed JSON welcome message.
     * @param[in,out] callback Response callback, consumed exactly once.
     */
    void hello(const drogon::HttpRequestPtr& /*req*/,
        std::function<void(const drogon::HttpResponsePtr&)>&&
            callback)  // NOLINT
    {
      drogon::HttpResponsePtr response =
          drogon::HttpResponse::newHttpResponse();  // Build an empty HTTP
                                                    // reply object.
      response->setBody(JsonHelper::jsonify("Welcome to the API!")
              .toStyledString());  // Turn the greeting text into a JSON
                                   // string and use it as the body.
      response->setStatusCode(
          drogon::k200OK);  // 200 means the request succeeded.
      response->setContentTypeCode(
          drogon::CT_APPLICATION_JSON);  // Mark the body as JSON so
                                         // clients parse it.
      std::move(callback)(
          response);  // Send the finished response to the client.
    }

    // Public liveness route: timing + rate limiting only, no
    // authentication.
    METHOD_LIST_BEGIN  // Start the block where Drogon route registrations
                       // are declared.
        METHOD_ADD(Hello::hello, "", drogon::Get,
            INSECURE);  // Bind GET of the controller
                        // path to hello() with the
                        // INSECURE filter chain.
    METHOD_LIST_END     // End the route registration block.
  };

}  // namespace api::v2
