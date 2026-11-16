/**
 * @file doc.hpp
 * @brief Serves the Swagger UI page that documents the API.
 * @details This file defines the Doc controller with a single endpoint. That
 * endpoint returns an HTML page containing the Swagger UI, an interactive
 * documentation viewer for the REST API. The HTML is embedded into the program
 * at build time from docs/api.html, so the running server does not need to read
 * any file from disk. The handler simply builds a 200 HTML response and hands
 * it to the callback; there is no per-request state or authentication.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the human-facing documentation endpoint of the API. Developers and
 * testers open it in a browser to see every available route, its parameters and
 * its response format. Drogon calls it at the /api/v2/doc URL; it uses no
 * database and no controllers. Because it goes through the INSECURE chain it is
 * rate-limited and timed but needs no token, so anyone can read the public
 * docs.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Swagger / OpenAPI: a standard way to describe REST APIs
 * (https://swagger.io/).
 * - HTTP GET and status 200:
 * https://developer.mozilla.org/en-US/docs/Web/HTTP/Methods
 * - CORS: https://developer.mozilla.org/en-US/docs/Web/HTTP/CORS
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Only process this header once per source file.
// Drogon controller base and the request/response/status types the handler
// uses.
// Base class drogon::HttpController used by Doc.
#include <drogon/HttpController.h>
// Request type (unused here but required by the handler signature).
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>  // Response type built by the handler.
#include <drogon/HttpTypes.h>     // Status and content-type constants.

#include <functional>  // std::function is the type of the response callback.
// std::string converts the embedded HTML into a response body.
#include <string>
#include <utility>  // std::move for consuming the callback.

// Brings in the INSECURE/SECURE filter macros used by METHOD_ADD.
#include "api/v2/basic/common.hpp"
// generated at configure time from docs/api.html (see
// tools/cmake/embed_html.cmake)
#include "api_html.hpp"
// The v2 API controllers live here.
namespace api::v2 {
  /**
   * @class Doc
   * @brief Serves the Swagger UI page that documents the API.
   * @details GET /api/v2/doc (INSECURE). The HTML is compiled into the binary
   * from docs/api.html at configure time, so no runtime file is required. The
   * handler only constructs the response; there is no request-dependent
   * state.
   * @see api_html.hpp (generated header containing docs::kApiHtml)
   */
  // GET /api/v2/doc  ->  the Swagger UI page describing the API. The page is
  // compiled into the binary.
  // Inherit Drogon's controller
  // template so Drogon
  // registers its routes.
  class Doc : public drogon::HttpController<Doc> {
   public:
    /**
     * @brief Respond with the embedded Swagger UI HTML document.
     * @param[in,out] callback Response callback, consumed exactly once.
     */
    void doc(const drogon::HttpRequestPtr& /*req*/,
             std::function<void(const drogon::HttpResponsePtr&)>&&
                 callback)  // NOLINT
    {
      drogon::HttpResponsePtr response =
          drogon::HttpResponse::newHttpResponse();  // Build an empty HTTP
                                                    // reply object.

      response->setBody(
          std::string(docs::kApiHtml));         // Put the compile-time embedded
                                                // Swagger HTML into the body.
      response->setStatusCode(drogon::k200OK);  // 200 means the page was found
                                                // and is being returned.
      response->setContentTypeCode(
          drogon::CT_TEXT_HTML);  // Mark the body as HTML so the browser
                                  // renders it.
      std::move(callback)(
          response);  // Send the response to the client and finish.
    }

    // Public documentation endpoint: timing + rate limiting only, no
    // authentication.
    METHOD_LIST_BEGIN  // Start the block where Drogon route registrations
                       // are declared.
        METHOD_ADD(Doc::doc,
                   "",
                   drogon::Get,
                   INSECURE);  // Bind GET of the controller
                               // path to doc() with the
                               // INSECURE filter chain.
    METHOD_LIST_END            // End the route registration block.
  };

}  // namespace api::v2
