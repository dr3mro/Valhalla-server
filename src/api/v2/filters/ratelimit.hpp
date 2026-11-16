/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file ratelimit.hpp
 * @brief Drogon filter that screens requests for DoS/rate-limit violations
 * before authentication.
 * @details This file defines the RateLimit filter, the first element of both
 * the INSECURE and SECURE chains. It collects facts about each request (IP
 * address, HTTP method, path, a hash of the headers and the body), bundles them
 * into a DOSDetector::Request and asks GateKeeper for a verdict. Allowed and
 * whitelisted requests continue to the next filter; blacklisted, rate limited,
 * banned or internally-failed requests are stopped immediately with an HTTP
 * error.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This filter is the server's first line of defence. Because Drogon runs it
 * before the Auth filter and before any handler, abusive clients are rejected
 * cheaply without spending time on login checks or database queries. It calls
 * GateKeeper, which forwards to DOSDetector; without it the API would have no
 * protection against floods of requests and every client would reach the
 * expensive code paths.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - HTTP filters/middleware: https://drogon.docsforge.com/master/filter/
 * - Denial-of-service (DoS) and rate limiting: limiting how many requests one
 * client may make.
 * - HTTP 429 Too Many Requests:
 * https://developer.mozilla.org/en-US/docs/Web/HTTP/Status/429
 * - std::shared_ptr: https://en.cppreference.com/w/cpp/memory/shared_ptr
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Base class drogon::HttpFilter used by this filter.
#include <drogon/HttpFilter.h>
// Request type supplying IP, method, path, headers and body.
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>  // Response type used for rejection replies.
// Status codes such as k403Forbidden and k429TooManyRequests.
#include <drogon/HttpTypes.h>
#include <drogon/drogon.h>  // Core Drogon definitions.
// FilterCallback and FilterChainCallback aliases.
#include <drogon/drogon_callbacks.h>

#include <functional>     // Callback machinery used by Drogon internally.
#include <memory>         // std::shared_ptr for the shared service objects.
#include <string>         // std::string for messages and request fields.
#include <unordered_map>  // Used by the header-hashing code in DOSDetector.
#include <utility>        // std::move for consuming the callback.

// Project headers: the DoS detector, the gatekeeper facade and the service
// locator.
// Defines DOSDetector::Request and DOSDetector::Status.
#include "gatekeeper/dosdetector/dosdetector.hpp"
// GateKeeper::isDosAttack returns the verdict.
#include "gatekeeper/gatekeeper.hpp"
// Store/service locator provides the shared objects.
#include "store/store.hpp"
namespace api::v2::Filters  // Filters for the v2 API live here.
{
  /**
   * @class RateLimit
   * @brief Drogon filter that screens requests for DoS/rate-limit violations
   * before authentication.
   * @details First element of both the INSECURE and SECURE chains. It builds
   * a DOSDetector::Request from the peer IP, method, path, hashed headers and
   * body, and asks GateKeeper for a verdict. ALLOWED/WHITELISTED continue the
   * chain; blacklist, rate limit, ban and internal error short-circuit with a
   * corresponding HTTP status. The GateKeeper and DOSDetector are shared
   * service-locator objects held for the filter's lifetime.
   * @note Unknown verdicts fall through to continuing the chain (default
   * branch).
   */
  class RateLimit
      : public drogon::HttpFilter<RateLimit>  // Inherit Drogon's filter
                                              // template so it can be
                                              // registered by name.
  {
   public:
    /** @brief Obtain the shared GateKeeper and DOSDetector from the service
     * locator. */
    RateLimit()  // Constructor fetches the two shared objects once, when
                 // the filter is created.
        : gateKeeper(Store::getObject<GateKeeper>()),
          dos_detector(
              Store::getObject<DOSDetector>())  // Look up both singletons
                                                // from the Store.
    {
    }
    /**
     * @brief Classify the request and continue the chain or send a
     * rejection response.
     * @details Invoked by Drogon on its IO thread; verdicts map as:
     * BLACKLISTED -> 403, RATELIMITED -> 429, BANNED -> 503, ERROR -> 500.
     * ALLOWED/WHITELISTED and any unrecognised status invoke @p fccb;
     * rejected requests invoke @p fcb only.
     * @param[in] req Request supplying peer IP, method, path, headers and
     * body for classification.
     * @param[in,out] fcb Filter callback used to send the rejection
     * response.
     * @param[in,out] fccb Chain callback invoked when the request is
     * permitted.
     */
    void doFilter(const drogon::HttpRequestPtr& req,
                  drogon::FilterCallback&&      fcb,
                  drogon::FilterChainCallback&& fccb)
        override  // Drogon calls this on every request passing through this
                  // filter.
    {
      DOSDetector::Request request =
          {.ip = req->peerAddr().toIp(),  // Designated initializers fill each
                                          // field by name: caller IP address.
           .method =
               req->methodString(),     // HTTP verb as text (GET, POST, ...).
           .path        = req->path(),  // URL path being requested.
           .header_hash = DOSDetector::hashHeaders(
               req->getHeaders()),  // Condense all headers into one number
                                    // to spot repeated patterns cheaply.
           .body = req->body()};    // Raw request body, used by the
                                    // detector's heuristics.

      // Ask the gatekeeper for a verdict; the detector details are
      // encapsulated there.
      DOSDetector::Status status = gateKeeper->isDosAttack(
          request);  // GateKeeper forwards to DOSDetector and returns an
                     // enum verdict.

      // Permit verdicts continue the chain; every rejection sends one
      // response and stops here.
      switch (status)  // Map each verdict to "continue" or a specific
                       // error reply.
      {
        case DOSDetector::Status::ALLOWED:      // Normal, permitted
                                                // traffic.
        case DOSDetector::Status::WHITELISTED:  // Explicitly trusted
                                                // caller.
          fccb();  // Continue to the next filter/handler.
          break;   // Leave the switch.
        case DOSDetector::Status::BLACKLISTED:  // Caller is on the deny
                                                // list.
          reply("IP is blacklisted.",
                drogon::k403Forbidden,
                std::move(fcb));                // Send 403 Forbidden.
          break;                                // Leave the switch.
        case DOSDetector::Status::RATELIMITED:  // Caller sent too many
                                                // requests.
          reply("Too many requests.",
                drogon::k429TooManyRequests,
                std::move(fcb));           // Send 429 with a short message.
          break;                           // Leave the switch.
        case DOSDetector::Status::BANNED:  // Caller is temporarily
                                           // banned.
          reply("IP is banned.",
                drogon::k503ServiceUnavailable,
                std::move(fcb));          // Send 503 Service Unavailable.
          break;                          // Leave the switch.
        case DOSDetector::Status::ERROR:  // The detector itself failed.
          reply("Server error.",
                drogon::k500InternalServerError,
                std::move(fcb));  // Send 500 Internal Server Error.
          break;                  // Leave the switch.
        default:                  // Any future/unknown verdict.
          fccb();  // Be permissive by default so new codes do not
                   // break all traffic.
          break;   // Leave the switch.
      }
    }

   private:
    std::shared_ptr<GateKeeper>
        gateKeeper; /**< Used by doFilter via isDosAttack(). */
    std::shared_ptr<DOSDetector>
        dos_detector; /**< Held for its lifetime only; doFilter never
                         dereferences it. */
    /**
     * @brief Send a small plain-text rejection response with the given
     * status.
     * @param[in] msg Response body text.
     * @param[in] code HTTP status code to set.
     * @param[in] cb Filter callback invoked with the response.
     */
    void reply(
        const std::string&&    msg,
        drogon::HttpStatusCode code,
        const drogon::FilterCallback&&
            cb)  // Small helper to avoid repeating response-building code.
    {
      auto resp =
          drogon::HttpResponse::newHttpResponse();  // Build an empty HTTP
                                                    // reply object.
      resp->setBody(msg);         // Put the short message in the body (plain
                                  // text, not JSON).
      resp->setStatusCode(code);  // Set the chosen HTTP status.
      cb(resp);  // Send the response, which stops the chain (the chain
                 // callback is never called).
    }
  };
}  // namespace api::v2::Filters
