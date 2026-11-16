/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file elapsedtime.hpp
 * @brief Timing middleware that logs how long a request took to produce its
 * response.
 * @details This file defines the ElapsedTime middleware, used in both the
 * INSECURE and SECURE chains. When a request arrives it records a start time
 * using a steady clock. It then passes the request down the chain and installs
 * a callback that runs on the way back out, when the downstream handler has
 * produced its response. That callback measures the elapsed milliseconds and
 * writes a coloured log line containing the time, client IP, HTTP method,
 *          status code and path, then forwards the response unchanged.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This middleware is the server's stopwatch. It is the only place that reports
 * how long each endpoint actually took, which is how developers spot slow
 * routes or database bottlenecks. It sits around the rest of the chain, so it
 * measures the full cost of rate limiting, authentication and the handler. It
 * changes nothing about the response; without it the server would still work
 * but you would be blind to performance problems.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - HTTP middleware: https://drogon.docsforge.com/master/filter/
 * - std::chrono steady clock (a clock that never jumps backwards):
 * https://en.cppreference.com/w/cpp/chrono/steady_clock
 * - Lambdas and captures: https://en.cppreference.com/w/cpp/language/lambda
 * - CORS (unrelated here, but part of the HTTP filter world):
 * https://developer.mozilla.org/en-US/docs/Web/HTTP/CORS
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Only process this header once per source file.
// Drogon middleware base and request/response/callback types.
// Base class drogon::HttpMiddleware used here.
#include <drogon/HttpMiddleware.h>
// Request type (we read IP, method and path from it).
#include <drogon/HttpRequest.h>
// Response type (we read the status code from it).
#include <drogon/HttpResponse.h>
#include <drogon/drogon.h>  // Core Drogon definitions.
// MiddlewareNextCallback and MiddlewareCallback aliases. Formatting and logging
// helpers for the coloured log line.
#include <drogon/drogon_callbacks.h>
#include <fmt/color.h>   // fmt::fg and fmt::color for coloured output.
#include <fmt/core.h>    // Core fmt formatting types.
#include <fmt/format.h>  // fmt::format builds the log string.
#include <trantor/utils/Logger.h>  // LOG_INFO macro writes to the server log.

#include <chrono>   // std::chrono::steady_clock and duration_cast for timing.
#include <utility>  // std::move for consuming the callbacks.

namespace api::v2::MiddleWares  // Middleware for the v2 API lives here (note
                                // the project's spelling).
{
  /**
   * @class ElapsedTime
   * @brief Timing middleware that logs how long a request took to produce its
   * response.
   * @details Part of both the INSECURE and SECURE chains. It records a
   * steady_clock start, passes the request onward, and measures the interval
   * in the return-path callback when the downstream handler's response
   * propagates back out. The result is logged with request IP, method, status
   * and path; the response is then forwarded unchanged. Stateless and safe
   * for concurrent requests: all per-request state lives in the callback
   * capture.
   */
  class ElapsedTime : public drogon::HttpMiddleware<
                          ElapsedTime>  // Inherit Drogon's middleware template
                                        // so it can be registered by name.
  {
   public:
    ElapsedTime() = default;  // do not omit constructor: Drogon needs a default
                              // constructor to create the middleware.

    /**
     * @brief Start the timer and install a return-path callback that logs
     * elapsed milliseconds.
     * @details Invoked by Drogon on its IO thread. The callback passed to
     * @p nextCb runs when the response comes back from later
     * middleware/handlers, so the measurement covers the whole downstream
     * chain. The response is forwarded untouched to @p mcb.
     * @param[in] req Request whose peer address, method and path are
     * included in the log line.
     * @param[in,out] nextCb Chain callback given the measuring wrapper.
     * @param[in,out] mcb Middleware callback that receives the final
     * response.
     */
    void invoke(const drogon::HttpRequestPtr&    req,
                drogon::MiddlewareNextCallback&& nextCb,
                drogon::MiddlewareCallback&&     mcb)
        override  // Drogon calls invoke to run this middleware.
    {
      // Store the start time for this request
      std::chrono::steady_clock::time_point start_time =
          std::chrono::steady_clock::now();  // Snapshot the monotonic
                                             // clock; captured by the
                                             // callback below.

      // will run while getting out from this middleware
      std::move(nextCb)(  // Continue down the chain, giving it our
                          // measuring callback to call on the way back.
          [mcb = std::move(mcb), start_time, req](
              const drogon::HttpResponsePtr&
                  resp)  // Capture the final callback, the start time and
                         // the request for later.
          {
            // Calculate the elapsed time for this request
            auto end_time =
                std::chrono::steady_clock::now();  // Take a second clock
                                                   // reading when the response
                                                   // returns.
            auto elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time)
                    .count();  // Difference converted to whole
                               // milliseconds for the log.
            // Log the elapsed time
            LOG_INFO << fmt::format(
                ": {}{}{}{}{}",
                fmt::styled(fmt::format("[{} ms] ", elapsed_ms),
                            fmt::fg(fmt::color::orange)),  // Build one line:
                                                           // elapsed time in
                                                           // orange.
                fmt::styled(fmt::format("[{}]", req->getPeerAddr().toIp()),
                            fmt::fg(
                                fmt::color::yellow)),  // Client IP in yellow.
                fmt::styled(fmt::format("[{}]", req->getMethodString()),
                            fmt::fg(fmt::color::red)),  // HTTP method in red.
                fmt::styled(fmt::format("[{}]",
                                        static_cast<int>(
                                            resp->getStatusCode())),
                            fmt::fg(fmt::color::magenta)),  // Response status
                                                            // in magenta.
                fmt::styled(fmt::format("[{}]", req->path()),
                            fmt::fg(
                                fmt::color::cyan)));  // Request path in cyan,
                                                      // then write to the log.
            std::move(mcb)(resp);  // Forward the unchanged response to
                                   // the previous middleware/route.
          });
    }
  };
}  // namespace api::v2::MiddleWares
