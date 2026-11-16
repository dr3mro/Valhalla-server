/**
 * @file asyncdberror.hpp
 * @brief Maps exceptions from the Drogon async database path to the HTTP
 * statuses of the synchronous path.
 * @details This header declares three small helpers that inspect an exception.
 * One decides whether the error means "the database is busy", one turns that
 * decision into an HTTP status code, and one throws the project's DatabaseBusy
 * exception so the rest of the server can keep a single error-handling path.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * The controller methods that use Drogon coroutines catch errors as the generic
 * std::exception type, so they lose the specific Drogon exception class. These
 * helpers recover the important distinction: a busy pool or a timeout means
 * "try again later" (HTTP 503 Service Unavailable), while any other failure is
 * a normal internal error (HTTP 500). They let the async database path answer
 * failures exactly the same way the older synchronous path does. Without this
 * file, a temporary database overload would be reported as a generic server
 * error instead of a retryable one, and callers would not know to back off.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - C++20 coroutines: https://en.cppreference.com/w/cpp/language/coroutines
 * - Exceptions and std::exception:
 * https://en.cppreference.com/w/cpp/error/exception
 * - dynamic_cast (checking an object's real type at runtime):
 * https://en.cppreference.com/w/cpp/language/dynamic_cast
 * - HTTP 503 status:
 * https://developer.mozilla.org/en-US/docs/Web/HTTP/Status/503
 * - Drogon DbClient: https://drogon.docsforge.com/master/database/
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Make sure these declarations appear only once in each compiled
              // source file.

// drogon::HttpStatusCode and the k500/k503 constants returned by
// toHttpStatus().
#include <drogon/HttpTypes.h>
// Drogon's exception classes, whose messages we examine.
#include <drogon/orm/Exception.h>

// std::exception, the base type every caught error arrives as.
#include <exception>
#include <string>  // std::string, used when searching the error message text.

// DatabaseBusy, the project's own "no connection available" exception.
#include "database/databasebusy.hpp"

/**
 * @namespace asyncdb
 * @brief Maps exceptions from the Drogon async database path to the HTTP
 * statuses of the synchronous path.
 * @details An exhausted pool or timeout is "busy" and maps to 503, matching
 * `DatabaseBusy`; every other failure maps to 500. The helpers take
 * `std::exception` because coroutine awaits surface their failures through that
 * base type.
 */
namespace asyncdb {
  /**
   * @brief Tests whether an exception represents a busy database (pool
   * exhaustion or timeout).
   * @details First checks for a `DatabaseBusy` by dynamic type; otherwise
   * performs a case-sensitive substring search of `what()` for "timeout",
   * "pool", "too many clients" and "connection". The message check is a
   * heuristic used because callers catch exceptions as `std::exception`,
   * where the specific Drogon type may not be available.
   * @param[in] error Exception to classify; need not outlive the call.
   * @return True if the exception looks like a busy/timeout condition.
   * @warning "connection" is a broad match: an unrelated error whose message
   * mentions a connection is classified as busy.
   */
  [[nodiscard]] bool isBusy(
      const std::exception&
          error);  // `[[nodiscard]]` forces callers to use the answer;
                   // `const&` inspects without copying.

  /**
   * @brief Converts an exception to the HTTP status the synchronous database
   * path would produce.
   * @param[in] error Exception to classify.
   * @return `k503ServiceUnavailable` when `isBusy(error)` is true, otherwise
   * `k500InternalServerError`.
   */
  [[nodiscard]] drogon::HttpStatusCode toHttpStatus(
      const std::exception&
          error);  // Returns a Drogon status constant the HTTP layer can send
                   // directly to the client.

  /**
   * @brief Raises `DatabaseBusy` when the exception represents an exhausted
   * pool or timeout.
   * @param[in] error Exception to classify.
   * @throws DatabaseBusy if `isBusy(error)` is true; otherwise the function
   * does nothing.
   * @note The original exception is not chained; callers rely on the
   * resulting `DatabaseBusy` producing a 503 with Retry-After in the HTTP
   * layer.
   */
  void throwIfBusy(
      const std::exception& error);  // Re-throws as the project's own type so
                                     // existing error handlers need no changes.
}  // namespace asyncdb

/** @brief Alias for the Phase 2 vocabulary; denotes the same namespace as
 * `asyncdb`. */
namespace AsyncDbError =
    asyncdb;  // Another name for the same helpers, used by code written during
              // "Phase 2" of the async migration.
