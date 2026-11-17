/**
 * @file cors.hpp
 * @brief CORS header helpers applied to every response by the pre-sending
 * advice in server.cpp.
 *
 * @details This header provides small functions that write the Cross-Origin
 * Resource Sharing (CORS) headers onto an outgoing HTTP response. CORS is the
 * browser rule that decides whether a web page on one domain may read a
 *          response from another domain. The code here reads the configured
 * list of allowed origins, reflects an exact match back with credentials, or
 * falls back to a wildcard or first configured origin. It also handles
 *          preflight (OPTIONS) requests by echoing a sanitized list of
 * requested headers.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Without these headers, a browser front end served from a different address
 * than the API would be blocked by its same-origin policy, even though the API
 * itself works. server.cpp calls Cors::apply() for every single response, so
 * this file is the one central place that makes the REST API reachable from a
 * web app. It also contains the small token helpers' entry points used by the
 * preflight logic, so CORS behaviour stays consistent and easy to audit.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - CORS (browser cross-origin rules):
 * https://developer.mozilla.org/en-US/docs/Web/HTTP/CORS
 * - Inline functions and function-local statics:
 * https://en.cppreference.com/w/cpp/language/inline
 * - std::string_view (a non-owning view over text):
 * https://en.cppreference.com/w/cpp/string/basic_string_view
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this file at most once per source file.

// for drogon::HttpRequestPtr, used to read Origin and preflight headers
#include <drogon/HttpRequest.h>
// for drogon::HttpResponsePtr and its addHeader/getHeader/headers methods
#include <drogon/HttpResponse.h>
// for drogon::Options, used to detect preflight requests
#include <drogon/HttpTypes.h>

#include <string>  // for std::string constants and return values
// for std::string_view parameters that borrow text without copying it
#include <string_view>
#include <vector>  // for std::vector<std::string> holding the split origin list

// for Configurator::FrontEndConfig and its cors_allowed_origins field
#include "configurator/configurator.hpp"
// for the Drogon-free token helpers mergeVary() and sanitizeRequestedHeaders()
#include "server/extras/cors_tokens.hpp"
// for Store::cachedObject<Configurator>() used to read live settings
#include "store/store.hpp"

/**
 * @namespace Cors
 * @brief Writes the CORS response headers onto every outgoing response.
 *
 * @details CORS response headers, written in exactly one place (the pre-sending
 * advice in server.cpp). Drogon stores headers in a map keyed by the
 * lower-cased name, and addHeader replaces an existing value
 * (HttpResponseImpl.h:194-210), so a header can never appear twice.
 */
// Group the CORS helper functions together without needing an
// object instance.
namespace Cors {
  /**
   * Short alias so call sites can use detail:: for the Drogon-free token
   * helpers.
   */
  namespace detail = CorsTokens;  // An alias: detail:: now means CorsTokens,
                                  // keeping call sites short.

  /**
   * @brief The method list advertised in Allow and
   * Access-Control-Allow-Methods.
   * @note PATCH is advertised here but Server::isConventionalMethod() rejects
   * it with 405.
   * @return A process-lifetime constant string, returned by reference to
   * avoid a copy per response.
   */
  // Return the fixed list of methods the server advertises
  // as allowed.
  inline const std::string& allowedMethods() {
    static const std::string value =
        // Built once and reused; inline means one copy in the program.
        "GET, POST, PUT, PATCH, DELETE, HEAD, OPTIONS";
    return value;  // Return by reference so no copy is made on every
                   // response.
  }

  /**
   * @brief The fallback Access-Control-Allow-Headers list used on
   * non-preflight responses.
   * @return A process-lifetime constant string, returned by reference to
   * avoid a copy per response.
   */
  // Return the standard list of request headers
  // allowed cross-origin.
  inline const std::string& defaultAllowedHeaders() {
    static const std::string value =
        // Created once at first use, not on each call.
        "Content-Type, Authorization, Accept, X-Requested-With";
    return value;  // Return by reference to avoid allocating a new string.
  }

  /**
   * @brief Headers browsers are allowed to expose to page scripts.
   * @return A process-lifetime constant string, returned by reference to
   * avoid a copy per response.
   */
  // Return the response headers that JavaScript is
  // permitted to read.
  inline const std::string& exposedHeaders() {
    static const std::string value =
        "Retry-After, Content-Length";  // A shared constant; built only
                                        // once.
    return value;  // Return by reference so callers do not pay for a copy.
  }

  /**
   * @brief The Access-Control-Max-Age value (seconds) emitted on preflight
   * responses.
   * @return A process-lifetime constant string; 7200 matches the browser cap.
   */
  // Return how long, in seconds, a browser may cache a
  // preflight result.
  inline const std::string& preflightMaxAge() {
    static const std::string value =
        "7200";    // browsers cap this at 2 hours anyway
    return value;  // Return by reference to keep the hot path copy-free.
  }

  /**
   * @brief Adds a token to the response's Vary header only if it is not
   * already present.
   *
   * @details Deduplication is delegated to CorsTokens::mergeVary; comparing
   * before writing avoids touching the header map on responses that already
   * carry the value.
   * @param[in,out] resp Response whose Vary header may be updated.
   * @param[in] value Header name to add, for example "Origin".
   */
  // Add one token to Vary only when it is missing.
  inline void addToVary(const drogon::HttpResponsePtr& resp,
                        std::string_view               value) {
    const std::string& existing =
        resp->getHeader("vary");  // Read the current Vary header (empty
                                  // string when absent).
    const std::string merged =
        detail::mergeVary(existing,
                          value);  // Reuse the helper that appends without
                                   // duplicates.
    // Only touch the response if the merge
    // actually changed the list.
    if (merged != existing) {
      resp->addHeader("Vary",
                      merged);  // Store the updated list back on the response.
    }
  }

  /**
   * @brief Splits the comma-separated cors_allowed_origins setting into
   * individual origins.
   *
   * @details Spaces are skipped and empty tokens discarded, so a trailing
   * comma or whitespace after a comma does not create a bogus origin. Order
   * is preserved because apply() falls back to the first entry.
   * @param[in] value Raw configuration value.
   * @return Origins in configuration order.
   */
  // Split the configured comma-separated string into origins.
  inline std::vector<std::string> splitOrigins(const std::string& value) {
    std::vector<std::string> out;  // Accumulates the origins found, keeping
                                   // their original order.
    std::string
        cur;  // Holds the characters of the origin currently being built.
    // Examine every character in the configured value.
    for (char c : value) {
      // A comma ends the current origin.
      if (c == ',') {
        if (!cur.empty())
          out.push_back(cur);  // Save a non-empty origin; ignore
                               // empty ones from stray commas.
        // Reset the buffer for the next origin.
        // Skip spaces but keep every other character.
        cur.clear();
      } else if (c != ' ') {
        cur.push_back(c);  // Add this character to the origin being built.
      }
    }
    if (!cur.empty())
      out.push_back(cur);  // Do not forget the final origin; no trailing
                           // comma ends it.
    return out;  // Return the list in configuration order for the caller to
                 // use.
  }

  /**
   * @brief Decorates a response with CORS headers; invoked once per response
   * from the pre-sending advice.
   *
   * @details If the response already carries Access-Control-Allow-Origin:
   * "*", it is left untouched. Otherwise the current cors_allowed_origins
   * setting is read from the Configurator. An exact match to the request's
   *          Origin is reflected back with Allow-Credentials and Vary:
   * Origin; if origins are configured but none match, the first configured
   * origin is used without credentials; if the setting is "*", the wildcard
   * is used. For OPTIONS requests a sanitized echo of
   * Access-Control-Request-Headers replaces the default header list, and
   * Max-Age plus Vary are added. Every write uses HttpResponse::addHeader,
   * which replaces an existing value rather than duplicating it.
   * @param[in] req Request being answered; supplies Origin and, for
   * preflights, the requested headers.
   * @param[in,out] resp Response to decorate.
   * @note No shared mutable state is modified here; the configuration is read
   * through Store's thread-local cache.
   * @see CorsTokens::sanitizeRequestedHeaders(), CorsTokens::mergeVary()
   */
  // Write the correct CORS headers onto this response.
  inline void apply(const drogon::HttpRequestPtr&  req,
                    const drogon::HttpResponsePtr& resp) {
    static const std::string originKey =
        "access-control-allow-origin";      // Drogon keys headers in lowercase;
                                            // built once at first use.
    const auto& headers = resp->headers();  // Reference to the header map so we
                                            // can inspect what is already set.
    const auto  originIt =
        headers.find(originKey);  // Look for an existing allow-origin header.
    // If a wildcard was already set, there is nothing to add.
    if (originIt != headers.end() && originIt->second == "*") {
      return;  // Leave the response untouched; another layer already
               // allowed every origin.
    }

    const std::string allowed = Store::cachedObject<Configurator>()
                                    ->get<Configurator::FrontEndConfig>()
                                    .cors_allowed_origins;  // Live CORS config.
    const std::string requestOrigin =
        req->getHeader("Origin");  // Get the Origin header the browser sent
                                   // (empty if none).

    bool useCredentials =
        false;  // Starts false; only an exact origin match will turn it on.
    std::string allowOrigin = "*";  // Default to wildcard; replaced if
                                    // specific origins are configured.
    // A concrete list is configured, so we must choose
    // one origin.
    if (allowed != "*") {
      auto origins = splitOrigins(allowed);  // Break the configured string into
                                             // individual origins.
      // Compare each configured origin
      // with the request's Origin.
      for (const auto& o : origins) {
        // An exact match is safe to reflect
        // and allow credentials for.
        if (!requestOrigin.empty() && o == requestOrigin) {
          allowOrigin    = requestOrigin;  // Reflect exactly what the
                                           // browser sent back to it.
          useCredentials = true;           // Credentials require an exact
                                           // origin, never a wildcard.
          break;  // Stop searching once a match is found.
        }
      }
      // No exact match, but a list exists: fall
      // back to its first entry.
      if (allowOrigin == "*" && !origins.empty()) {
        allowOrigin = origins.front();  // Use the first configured
                                        // origin, without credentials.
      }
    }

    // Write the header only if missing or different.
    if (originIt == headers.end() || originIt->second != allowOrigin) {
      resp->addHeader("Access-Control-Allow-Origin",
                      allowOrigin);  // Tell the browser which origin may read
                                     // this response.
    }
    // Add the credentials flag only for an exact
    // origin match.
    if (useCredentials) {
      resp->addHeader("Access-Control-Allow-Credentials",
                      "true");  // Allow cookies/auth to be sent cross-origin.
      addToVary(resp,
                "Origin");  // Vary on Origin so caches keep separate copies per
                            // origin.
    }
    resp->addHeader("Access-Control-Allow-Methods",
                    allowedMethods());  // Advertise the HTTP methods permitted
                                        // cross-origin.
    resp->addHeader("Access-Control-Expose-Headers",
                    exposedHeaders());  // List the response headers scripts may
                                        // read.

    const std::string& requested = req->getHeader(
        "Access-Control-Request-Headers");  // Preflight field naming the
                                            // headers the browser wants to
                                            // send.

    const std::string* allowedHeaders =
        &defaultAllowedHeaders();  // By default, advertise the standard
                                   // header list.
    std::string echoed;  // Will hold the sanitized copy of the client's
                         // requested headers.
    // Only preflight requests carry
    // Access-Control-Request-Headers.
    if (req->method() == drogon::Options) {
      echoed = detail::sanitizeRequestedHeaders(
          requested);  // Filter the request value down to safe tokens.
      // Use the echo only if some valid tokens
      // survived filtering.
      if (!echoed.empty()) {
        allowedHeaders = &echoed;  // Point at the echoed list instead
                                   // of the default one.
      }
      resp->addHeader("Access-Control-Max-Age",
                      preflightMaxAge());  // Tell the browser how long it may
                                           // cache this preflight.
      addToVary(resp,
                "Access-Control-Request-Headers");  // Vary on requested headers
                                                    // so caches do not mix them
                                                    // up.
    }
    resp->addHeader("Access-Control-Allow-Headers",
                    *allowedHeaders);  // Write the final list: default or
                                       // sanitized echo.
  }
}  // namespace Cors
