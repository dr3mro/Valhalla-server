/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file common.hpp
 * @brief Safely parse a numeric id query parameter, replying 400 on failure.
 * @details This header holds the small pieces of code that almost every v2
 * route reuses. First it defines the filter/middleware name macros (RATELIMIT,
 * AUTH, ELAPSED, INSECURE, SECURE) that controllers pass to Drogon's
 * METHOD_ADD. It also provides parseIdParam, which turns an "id" query
 * parameter into a number without letting an exception crash the request.
 *          Finally it provides executeControllerMethod, the shared helper that
 * calls a controller, adapts its numeric reply into an HTTP JSON response, and
 * moves slow database work off the IO thread.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Every REST controller in this API includes this header. It is the common
 * plumbing between the HTTP layer (Drogon) and the business layer (the
 * controllers/entities). The route controllers call parseIdParam and
 * executeControllerMethod; those in turn use Helper for JSON responses,
 * DbExecutor for background work and GateKeeper-provided attributes for
 * identity. Without this file each of the seven controllers would have to
 * duplicate dozens of lines of tricky error handling and threading.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - HTTP filters/middleware: https://drogon.docsforge.com/master/filter/
 * - Query parameters: values appended to a URL after "?" (e.g. ?id=42).
 * - C++ templates: https://en.cppreference.com/w/cpp/language/templates
 * - std::optional: https://en.cppreference.com/w/cpp/utility/optional
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Only process this header once per source file.

// Drogon headers for the request object, response object and HTTP status codes.
// Request type passed to parseIdParam and the dispatcher.
#include <drogon/HttpRequest.h>
// Response type handed to the completion callback.
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>  // HTTP status constants such as k400BadRequest.
// Main Drogon umbrella header (convenient, brings in core types).
#include <drogon/drogon.h>
#include <fmt/core.h>    // String formatting used to build error messages.
#include <fmt/format.h>  // The fmt::format function used in error text.

#include <cstdint>    // Fixed-width integer types such as int64_t and uint64_t.
#include <exception>  // std::exception, the base of all exceptions we catch.
#include <functional>  // std::function, the type of the response callback.
#include <memory>  // Smart-pointer helpers used indirectly by the controllers.
#include <optional>  // std::optional, used to return "an id or nothing".
#include <string>    // std::string for parameter names and messages.
// std::string_view, a cheap non-owning view of the type key.
#include <string_view>
// std::tuple/std::make_tuple/std::apply pack the controller arguments.
#include <tuple>
#include <type_traits>  // std::decay_t normalises forwarded argument types.
// std::move/std::forward for transferring callbacks and arguments.
#include <utility>

// Project headers used by the dispatcher and the error helpers.
// Helper builds the standard JSON success/failure/error responses.
#include "api/v2/helper/helper.hpp"
// DatabaseBusy exception -> retryable 503 response.
#include "database/databasebusy.hpp"
// DbExecutor runs blocking database work on worker threads.
#include "database/dbexecutor.hpp"
// DbProbe detects which calls need a database connection.
#include "database/dbprobe.hpp"
// CALLBACK_ alias: the controller's (int code, string body) callback.
#include "utils/global/callback.hpp"
#include "utils/global/http.hpp"  // api::v2::Http::Status numeric result codes.
// Requester carries the caller's id and group for permissions.
#include "utils/global/requester.hpp"

// Filter/middleware names passed to Drogon's METHOD_ADD. The strings are the
// registered class names; INSECURE = rate limiting + elapsed-time logging,
// SECURE = INSECURE + token auth. Routes whose handlers rely on
// "clientID"/"clientGroup" request attributes must use SECURE.
#define RATELIMIT \
  "api::v2::Filters::RateLimit"  // Fully-qualified name Drogon uses to find
                                 // the DoS filter.
#define AUTH \
  "api::v2::Filters::Auth"  // Fully-qualified name of the Bearer-token
                            // filter.
#define ELAPSED \
  "api::v2::MiddleWares::ElapsedTime"  // Fully-qualified name of the timing
                                       // middleware.

// A macro may expand to a comma-separated list, which is exactly what
// METHOD_ADD expects.
#define INSECURE \
  RATELIMIT, ELAPSED  // Public routes: rate limit + timing, no login required.
#define SECURE INSECURE, AUTH  // Protected routes: also require a valid token.

namespace api::v2  // Everything in the v2 API lives in this namespace to avoid
                   // name clashes.
{
  /**
   * @brief Safely parse a numeric id query parameter, replying 400 on
   * failure.
   * @details Calling std::stoll directly on req->getParameter(name) would
   * throw std::invalid_argument or std::out_of_range while building the
   * handler's argument list, i.e. before executeControllerMethod's try/catch
   * is entered; the exception then escaped Drogon's dispatch as a raw,
   * empty-body 500. This helper contains the throw and emits a normal JSON
   * 400 instead. On failure the callback has already been invoked with the
   * error response.
   * @param[in] req Request whose query parameter is read.
   * @param[in] name Parameter name to parse as an integer id.
   * @param[in,out] callback Response callback; invoked with a 400 when
   * parsing fails.
   * @return The parsed id, or std::nullopt on failure (callback already
   * answered).
   * @post If std::nullopt is returned the caller must return early without
   * calling executeControllerMethod, otherwise a second response would be
   * sent.
   */
  inline std::optional<int64_t>
  parseIdParam(  // "inline" lets this live in a header without
                 // duplicate-definition errors.
      const drogon::HttpRequestPtr& req,
      const std::string&            name,
      std::function<void(const drogon::HttpResponsePtr&)>&
          callback)  // req supplies the parameter; callback is answered on
                     // error.
  {
    const std::string& raw =
        req->getParameter(name);  // Read the raw text value (empty string
                                  // when the parameter is missing).
    try  // Guard the conversion because stoll throws on bad input.
    {
      return std::stoll(raw);  // Convert text like "42" into a 64-bit integer;
                               // throws if it is not a valid number.
    }
    catch (const std::exception&)  // Catches invalid_argument (not a
                                   // number) and out_of_range (too big).
    {
      Helper::errorResponse(drogon::k400BadRequest,
                            fmt::format("Missing or invalid '{}' parameter.",
                                        name),
                            std::move(callback));  // Send a friendly JSON 400
                                                   // and consume the callback.
      return std::nullopt;  // Signal failure so the caller returns early
                            // (the response is already sent).
    }
  }

  /**
   * @brief Dispatch a controller method for a keyed entity type, off the IO
   * thread when needed.
   * @details Looks up @p key in @p registry (an unordered_map of std::variant
   * controller pointers); a missing key is answered with 400 and no
   * controller is invoked. A model callback (int status + content) is adapted
   * to HTTP responses: OK -> success, INTERNAL_SERVER_ERROR -> failure (500
   * body), anything else -> error with that numeric status. Requester
   * identity is read from the "clientID"/"clientGroup" attributes that the
   * Auth filter inserts on SECURE routes (INSECURE routes such as client
   * create/login also use this helper). When executor.ioFastPath() is enabled
   * the call is first tried on the IO thread inside a DbProbe: work that
   * needs no connection completes there, whereas the first WouldBlock is
   * caught and the whole call is re-queued on the DB executor from the start.
   * Request, arguments and callback are captured by value because queued work
   * finishes after this function returns on a worker thread. A full executor
   *          queue is answered 503 without running the controller;
   * DatabaseBusy becomes 503, and any other std::exception becomes a 500
   * failure response.
   * @tparam Func Member-function pointer type (e.g. &ControllerBase::Create).
   * @tparam Registry Map type from entity-type key to a std::variant of
   * controller pointers.
   * @tparam Args Types of the extra arguments forwarded to the controller
   * method.
   * @param[in] executor Pool that runs blocking permission checks and
   * queries.
   * @param[in] registry Entity-type -> controller variant map.
   * @param[in] key Registry key selected from the request path.
   * @param[in] method Controller member function to invoke.
   * @param[in] req Request supplying attributes and body; captured by value
   * for async work.
   * @param[in,out] callback Response callback; consumed exactly once on every
   * path.
   * @param[in] args Extra arguments forwarded to the controller (e.g. body,
   * parsed id).
   */
  template <typename Func,
            typename Registry,
            typename... Args>  // Generic over the method, the map and any extra
                               // arguments.
  static void executeControllerMethod(
      DbExecutor&                                           executor,
      const Registry&                                       registry,
      const std::string_view                                key,
      Func                                                  method,
      const drogon::HttpRequestPtr&                         req,
      std::function<void(const drogon::HttpResponsePtr&)>&& callback,
      Args&&... args)  // "static" at namespace scope
                       // gives this helper internal
                       // linkage.
  {
    auto ctl = registry.find(key);  // Look up the controller variant for
                                    // this URL's type segment.
    if (ctl == registry.end())      // An unknown type key cannot be dispatched.
    {
      Helper::errorResponse(drogon::k400BadRequest,
                            fmt::format("Type mapping not found for: {}", key),
                            std::move(
                                callback));  // Tell the client the type is
                                             // unknown (400) and finish.
      return;  // Nothing else to do; the callback was already answered.
    }

    try  // Whole dispatch is guarded so no exception escapes to Drogon as
         // an empty 500.
    {
      std::visit(  // std::variant holds one of several controller pointer
                   // types; visit picks the right one.
          [&](const auto& controller)  // This lambda is called with whichever
                                       // controller type is stored.
          {
            // Adapts the controller's numeric status callback to an
            // HTTP response.
            CALLBACK_ mcb =
                [callback](int code,
                           const std::string&
                               content)  // The controller replies with an
                                         // int code and a JSON string.
            {
              using Reply = std::function<void(
                  const drogon::HttpResponsePtr&)>;  // Short alias for the
                                                     // callback type we must
                                                     // pass to Helper.
              switch (code)  // Translate the numeric result into the
                             // matching JSON HTTP reply.
              {
                // Controller succeeded.
                case api::v2::Http::Status::OK:
                  Helper::successResponse(content,
                                          Reply(callback));  // Send 200 with
                                                             // the body as-is.
                  break;  // Leave the switch.
                case api::v2::Http::Status::
                    INTERNAL_SERVER_ERROR:  // Controller hit an
                                            // internal error.
                  Helper::failureResponse(content,
                                          Reply(
                                              callback));  // Send a tagged 500
                                                           // failure body.
                  break;                                   // Leave the switch.
                default:  // Any other code is a specific error
                          // (e.g. 400, 403, 503).
                  Helper::errorResponse(static_cast<drogon::HttpStatusCode>(
                                            code),
                                        content,
                                        Reply(
                                            callback));  // Reuse that number as
                                                         // the HTTP status.
                  break;                                 // Leave the switch.
              }
            };

            // Read what the handler needs from the request now; the
            // rest runs on a worker.
            Requester
                requester(req->getAttributes()->get<uint64_t>("clientID"),
                          req->getAttributes()->get<std::string>(
                              "clientGroup"));  // Auth filter stored these;
                                                // build the identity used for
                                                // permission checks.

            auto packed = std::make_tuple(std::decay_t<Args>(
                args)...);  // Bundle the extra arguments so we can
                            // store/copy them for async work.

            // Fast path: run the call here first. A request that is
            // answered from memory (bad body, refused by the permission
            // cache) never queues; the first query throws WouldBlock
            // before anything was written, and the request goes to the
            // executor and runs again from the start.
            if (executor.ioFastPath())  // Is the cheap in-thread path
                                        // enabled?
            {
              try  // The probe call may throw; handle each case
                   // below.
              {
                DbProbe   probe;  // Guard object that throws WouldBlock
                                  // if a real DB connection is needed.
                CALLBACK_ probe_cb =
                    mcb;  // its own copy: the real one must still
                          // be there if the probe gives up
                std::apply(
                    [&](auto&... unpacked)
                    {
                      std::invoke(method,
                                  controller.get(),
                                  std::move(probe_cb),
                                  std::move(requester),
                                  unpacked...);
                    },
                    packed);  // Try the controller right here on
                              // the IO thread.
                return;       // It completed without needing the
                              // database, so we are done.
              }
              catch (const WouldBlock&)  // The probe says a
                                         // connection is required.
              {
                // needs the database: queue it below
              }
              catch (const std::exception& e)  // Some other error happened on
                                               // the fast path.
              {
                Helper::failureResponse(e.what(),
                                        std::move(
                                            callback));  // Reply 500 with the
                                                         // exception text.
                return;  // Done; do not also queue the call.
              }
            }

            // Queue the call on the DB workers when the fast path was
            // disabled or gave up; runOnDbExecutor answers 503 itself
            // if the bounded queue is full.
            runOnDbExecutor(
                executor,
                std::move(mcb),  // Hand the response adapter to the
                                 // executor wrapper.
                [method,
                 target = controller.get(),
                 req,
                 callback,
                 requester = std::move(requester),
                 packed    = std::move(packed)](
                    CALLBACK_&& cb) mutable  // Capture everything by
                                             // value: this runs later
                                             // on another thread.
                {
                  try  // The queued controller call can also fail.
                  {
                    std::apply(
                        [&](auto&... unpacked)
                        {
                          std::invoke(method,
                                      target,
                                      std::move(cb),
                                      std::move(requester),
                                      unpacked...);
                        },
                        packed);  // Run the real controller method
                                  // now that DB access is allowed.
                  }
                  catch (const DatabaseBusy&
                             e)  // The connection pool is exhausted.
                  {
                    // Exhausted connection pool -> retryable 503
                    // (Helper adds Retry-After).
                    Helper::errorResponse(drogon::k503ServiceUnavailable,
                                          e.what(),
                                          std::move(
                                              callback));  // Tell the client to
                                                           // try again shortly.
                  }
                  catch (const std::exception& e)  // Any other failure.
                  {
                    Helper::failureResponse(e.what(),
                                            std::move(
                                                callback));  // Reply 500 with
                                                             // the error text.
                  }
                });
          },
          ctl->second);  // Pass the selected controller variant to
                         // std::visit.
    }
    catch (const std::exception&
               e)  // Backstop for errors while setting up the visit.
    {
      Helper::failureResponse(e.what(),
                              std::move(
                                  callback));  // Always answer exactly once.
    }
  }
}  // namespace api::v2
