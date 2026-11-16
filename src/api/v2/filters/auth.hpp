/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file auth.hpp
 * @brief Drogon filter that validates the Bearer token and identifies the
 * requester.
 * @details This file defines the Auth filter, the last step of the SECURE
 * filter chain. It reads the Authorization header, checks that it starts with
 * "Bearer ", and asks GateKeeper whether the token is valid. It supports two
 * modes: a normal blocking check and an asynchronous coroutine check chosen by
 * the database configuration. On success it writes "clientID" and "clientGroup"
 * onto the request so controllers know who is calling; on failure it sends a
 *          JSON 401 (or a 503 when the database is too busy).
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the gatekeeper of the whole protected API. Drogon calls it before any
 * SECURE route handler, so it is the component that turns an anonymous HTTP
 * request into an identified, permission-checkable one. Downstream controllers
 * (clients, clinics, staff and so on) read the identity attributes it sets and
 * would have no idea who the caller was without it. It calls GateKeeper for
 * validation and the Store/service locator to find shared objects. Without this
 * file every protected route would be open to anyone.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - HTTP filters/middleware: https://drogon.docsforge.com/master/filter/
 * - Bearer tokens and the Authorization header:
 * https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Authorization
 * - C++ coroutines (co_await):
 * https://en.cppreference.com/w/cpp/language/coroutines
 * - HTTP 401 Unauthorized:
 * https://developer.mozilla.org/en-US/docs/Web/HTTP/Status/401
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Base class drogon::HttpFilter used by this filter.
#include <drogon/HttpFilter.h>
// Request type carrying the Authorization header.
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>  // Response type used for rejection replies.
// Status codes such as k401Unauthorized and k503ServiceUnavailable.
#include <drogon/HttpTypes.h>
#include <drogon/drogon.h>  // Core Drogon definitions.
// FilterCallback and FilterChainCallback aliases.
#include <drogon/drogon_callbacks.h>

#include <cstddef>  // size_t for string lengths.
#include <memory>   // std::shared_ptr for holding the shared GateKeeper.
// std::optional<Types::ClientLoginData> for the parsed credentials.
#include <optional>
#include <string>   // std::string for the token and messages.
#include <utility>  // std::move for consuming callbacks.

// Project headers: configuration, error types, coroutine support, gatekeeper,
// service locator and JSON.
// Reads DatabaseConfig::async to pick blocking vs coroutine mode.
#include "configurator/configurator.hpp"
// DatabaseBusy exception -> retryable 503 reply.
#include "database/databasebusy.hpp"
// drogon::async_run and drogon::Task used in async mode.
#include "drogon/utils/coroutine.h"
// GateKeeper::isAuthenticationValid(Async) validates the token.
#include "gatekeeper/gatekeeper.hpp"
// Types::ClientLoginData holds token, IP and parsed identity.
#include "gatekeeper/types.hpp"
// Store/service locator provides the shared objects.
#include "store/store.hpp"
// Turns a message string into a JSON body.
#include "utils/jsonhelper/jsonhelper.hpp"
namespace api::v2::Filters  // Filters for the v2 API live here.
{
  /**
   * @class Auth
   * @brief Drogon filter that validates the Bearer token and identifies the
   * requester.
   * @details Runs last in the SECURE filter chain, after rate limiting and
   * timing (see INSECURE/SECURE in basic/common.hpp). It rejects
   * malformed/missing Authorization headers and invalid tokens with a JSON
   * 401; a DatabaseBusy from the validator becomes a retryable JSON 503 with
   * Retry-After: 1. On success it inserts "clientID" and "clientGroup"
   * request attributes that downstream controllers read to build a Requester
   * for permission checks, then continues the chain. When
   * DatabaseConfig::async is set, validation runs in a drogon::Task coroutine
   * and the callbacks are moved into the coroutine frame so they outlive
   * every suspension.
   * @note All routes that consume the identity attributes must include this
   * filter (SECURE).
   */
  class Auth
      : public drogon::HttpFilter<Auth>  // Inherit Drogon's filter template
                                         // so Drogon can discover and run it.
  {
   public:
    /** @brief Default-constructs the filter; the GateKeeper is obtained
     * from the service locator. */
    Auth() = default;  // No custom setup needed; the member initializer
                       // below fetches the GateKeeper.
    /**
     * @brief Validate the request's Bearer token, then continue or
     * short-circuit the chain.
     * @details Extracts the token and peer IP, checks the "Bearer " prefix,
     * and calls GateKeeper either synchronously or as a coroutine depending
     * on DatabaseConfig::async. Success stores "clientID"/"clientGroup" on
     * the request and invokes @p fccb; any failure invokes @p fcb with a
     * JSON error response. This override is invoked by Drogon on its IO
     * thread; in async mode the remainder runs on the coroutine frame.
     * @param[in] req Request carrying the Authorization header and peer
     * address.
     * @param[in,out] fcb Filter callback used to send a rejection response
     * (consumed on reject).
     * @param[in,out] fccb Chain callback invoked on success to run the next
     * filter/handler.
     */
    void doFilter(const drogon::HttpRequestPtr& req,
        drogon::FilterCallback&&                fcb,
        drogon::FilterChainCallback&&           fccb)
        override  // Drogon calls this for every request on the filter
                  // chain.
    {
      const std::string bearer =
          "Bearer ";  // The exact prefix a valid Authorization header
                      // must start with.
      const size_t bearerMagic =
          bearer.length();  // Length of the prefix (7); used to split
                            // prefix from token.

      // Directly get the authorization header value to avoid
      // multiple dereferences
      const auto& auth_header =
          req->getHeader("Authorization");  // Read the header once into a
                                            // reference; empty if absent.

      // Check for "Bearer " prefix using a fast substring
      // comparison
      if (auth_header.empty() || auth_header.size() <= bearerMagic ||
          auth_header[bearerMagic - 1] != ' ' ||
          auth_header.substr(0, bearerMagic) !=
              bearer)  // Reject missing, too-short or wrong-prefix
                       // headers before any DB work.
      {
        // Malformed or non-Bearer header -> short-circuit with a JSON
        // 401.
        auto resp =
            drogon::HttpResponse::newHttpResponse();  // Build an empty HTTP
                                                      // reply object; Drogon
                                                      // sends it when we invoke
                                                      // the callback.
        resp->setStatusCode(
            drogon::HttpStatusCode::k401Unauthorized);  // 401 means "you did
                                                        // not authenticate".
        resp->setContentTypeCode(
            drogon::CT_APPLICATION_JSON);  // Mark the body as JSON so
                                           // clients parse it
                                           // correctly.
        resp->setBody(JsonHelper::stringify(
            JsonHelper::jsonify("failed to extract token, or not a bearer "
                                "token")));  // Wrap the message in JSON and
                                             // store it as the body.
        std::move(fcb)(resp);  // Send the rejection and stop the chain
                               // (the handler never runs).
        return;  // Leave doFilter; the callback was consumed exactly
                 // once.
      }

      // Extract the token
      std::optional<Types::ClientLoginData> clientLoginData =
          Types::ClientLoginData{};  // Start with an empty credentials
                                     // holder.
      clientLoginData->token = auth_header.substr(
          bearerMagic);  // Everything after "Bearer " is the raw token.
      clientLoginData->ip_address =
          req->getPeerAddr().toIp();  // The caller's IP is needed for
                                      // IP-bound validation.
      std::string       message;      // Filled with a human-readable reason if
                                      // validation fails.
      // Validate token. The DB mode is fixed for the process lifetime, so
      // read it once.
      static const bool asyncDb = Store::cachedObject<Configurator>()
                                      ->get<Configurator::DatabaseConfig>()
                                      .async;  // Cached so the setting is read
                                               // only once, not per request.
      if (asyncDb)  // Choose the coroutine path when asynchronous
                    // database access is configured.
      {
        const size_t bearerMagicLocal =
            bearerMagic;  // Copy by value so the coroutine does not
                          // capture a reference to a local.
        std::shared_ptr<GateKeeper> gateKeeperLocal =
            gateKeeper;  // Copy the shared pointer so the GateKeeper
                         // stays alive in the coroutine.
        // Move the callbacks into storage owned by the coroutine frame:
        // async_run moves the factory (and its captures) into the
        // coroutine, so they outlive every suspension.
        drogon::async_run(
            [req, bearerMagicLocal, gateKeeperLocal, fcb = std::move(fcb),
                fccb = std::move(fccb)]() mutable
                -> drogon::Task<void>  // Capture everything by value;
                                       // mutable lets us move the
                                       // callbacks out later.
            {
              std::optional<Types::ClientLoginData> clientLoginData =
                  Types::ClientLoginData{};  // Rebuild credentials
                                             // inside the coroutine
                                             // (its own stack frame).
              clientLoginData->token =
                  req->getHeader("Authorization")
                      .substr(bearerMagicLocal);  // Re-read the token
                                                  // using the saved
                                                  // prefix length.
              clientLoginData->ip_address =
                  req->getPeerAddr().toIp();  // Re-read the peer IP
                                              // inside the coroutine.
              std::string message;  // Reason text for a failed validation.
              bool        valid = false;  // Result of the validation call.
              drogon::FilterCallback cb =
                  std::move(fcb);  // Move the rejection callback into the
                                   // coroutine so it survives suspension.
              drogon::FilterChainCallback chain =
                  std::move(fccb);  // Move the success callback in as well.
              try                   // Validation may throw when the database is
                                    // overloaded.
              {
                valid = co_await gateKeeperLocal->isAuthenticationValidAsync(
                    clientLoginData,
                    message);  // Suspend until the async
                               // check finishes; fill valid
                               // and message.
              }
              catch (const DatabaseBusy& e)  // Too many DB requests in flight.
              {
                auto resp =
                    drogon::HttpResponse::newHttpResponse();  // Build the retry
                                                              // response.
                resp->setStatusCode(drogon::HttpStatusCode::
                        // 503 tells the client the server is
                        // temporarily unavailable.
                    k503ServiceUnavailable);
                resp->setContentTypeCode(
                    // Body is JSON.
                    drogon::CT_APPLICATION_JSON);
                resp->addHeader("Retry-After",
                    "1");  // Ask the client to retry in one second.
                resp->setBody(JsonHelper::stringify(
                    JsonHelper::jsonify(e.what())));  // Send the exception text
                                                      // as JSON.
                std::move(cb)(resp);  // Deliver the 503 and end this path.
                co_return;            // Exit the coroutine (do not continue
                                      // the chain).
              }
              if (!valid)  // The token was rejected.
              {
                auto resp =
                    drogon::HttpResponse::newHttpResponse();  // Build the
                                                              // unauthorized
                                                              // response.
                resp->setStatusCode(drogon::HttpStatusCode::
                        k401Unauthorized);  // 401 = not
                                            // authenticated.
                resp->setContentTypeCode(
                    // Body is JSON.
                    drogon::CT_APPLICATION_JSON);
                resp->setBody(JsonHelper::stringify(
                    JsonHelper::jsonify(message)));  // Explain why validation
                                                     // failed.
                std::move(cb)(resp);                 // Send the 401.
                co_return;  // Stop here; the handler must not run.
              }

              // Populate the identity attributes consumed by
              // controllers' permission checks.
              req->attributes()->insert("clientID",
                  clientLoginData->clientId
                      .value());  // Store the authenticated client id
                                  // on the request.
              req->attributes()->insert("clientGroup",
                  clientLoginData->group
                      .value());  // Store the client's group used for
                                  // permission checks.

              // Token is valid, pass control to the next
              // filter/handler
              std::move(chain)();  // Continue the chain so the actual
                                   // route handler runs.
            });
        return;  // The coroutine owns the rest; leave doFilter now.
      }

      bool valid = false;  // Blocking-mode result of the validation call.
      try                  // Guard against a busy database.
      {
        valid = gateKeeper->isAuthenticationValid(clientLoginData,
            message);  // Blocking check; fills valid and message.
      }
      catch (const DatabaseBusy& e)  // Connection pool exhausted.
      {
        auto resp =
            // Build the retry response.
            drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(
            drogon::HttpStatusCode::k503ServiceUnavailable);  // 503 = try again
                                                              // soon.
        resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);  // Body is JSON.
        resp->addHeader(
            "Retry-After", "1");  // Suggest retrying after one second.
        resp->setBody(JsonHelper::stringify(
            JsonHelper::jsonify(e.what())));  // Send the error text as JSON.
        std::move(fcb)(resp);                 // Deliver the 503.
        return;                               // Stop; no handler runs.
      }
      if (!valid)  // Token rejected.
      {
        auto resp =
            drogon::HttpResponse::newHttpResponse();  // Build the unauthorized
                                                      // response.
        resp->setStatusCode(
            drogon::HttpStatusCode::k401Unauthorized);  // 401 = not
                                                        // authenticated.
        resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);  // Body is JSON.
        resp->setBody(JsonHelper::stringify(
            JsonHelper::jsonify(message)));  // Explain the failure.
        std::move(fcb)(resp);                // Send the 401.
        return;                              // Stop; no handler runs.
      }

      req->attributes()->insert("clientID",
          clientLoginData->clientId.value());  // Store the authenticated client
                                               // id for controllers.
      req->attributes()->insert("clientGroup",
          clientLoginData->group.value());  // Store the client's group
                                            // for permission checks.

      // Token is valid, pass control to the next filter/handler
      std::move(fccb)();  // Continue the chain so the route handler runs.
    }

   private:
    std::shared_ptr<GateKeeper> gateKeeper =
        Store::getObject<GateKeeper>();  // Shared validator fetched once
                                         // from the service locator.
  };
}  // namespace api::v2::Filters
