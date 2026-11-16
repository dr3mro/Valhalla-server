/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file asyncdberror.cpp
 * @brief Implementation for asyncdberror.
 * @details This file implements the three error-classification helpers.
 * isBusy() first recognises our own DatabaseBusy exception and then looks for a
 * few telling words in the error text (such as "timeout" or "pool").
 * toHttpStatus() turns that answer into an HTTP code, and throwIfBusy()
 * re-raises the project's DatabaseBusy type so the shared HTTP error handler
 * can produce a 503 response.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Every coroutine-based database method in DatabaseController catches a plain
 * std::exception when a query fails. This file is what lets those methods tell
 * a temporary overload apart from a real bug. Because both the old synchronous
 * pool and the new async path end up producing the same DatabaseBusy exception,
 * the HTTP layer needs only one rule for answering 503 with Retry-After.
 * Without this file, async database failures would all look alike and temporary
 * outages would be reported as permanent server errors.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Exceptions and catching by base class:
 * https://en.cppreference.com/w/cpp/error/exception
 * - dynamic_cast (runtime type check):
 * https://en.cppreference.com/w/cpp/language/dynamic_cast
 * - std::string::find and npos:
 * https://en.cppreference.com/w/cpp/string/basic_string/find
 * - HTTP 503 status:
 * https://developer.mozilla.org/en-US/docs/Web/HTTP/Status/503
 * - Drogon DbClient: https://drogon.docsforge.com/master/database/
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Our own declarations, so the compiler checks they match these definitions.
#include "database/asyncdberror.hpp"

// Drogon's exception messages are what we search for busy/timeout wording.
#include <drogon/orm/Exception.h>

// std::string and its find() method, used to scan the error message.
#include <string>

namespace asyncdb
{
  bool isBusy(const std::exception& error)
  {
    // A DatabaseBusy thrown by our own code is authoritative; Drogon
    // failures are recognised by message text. "Authoritative" means: if
    // our own code raised it, we trust the type and do not need to guess
    // from words.
    if (dynamic_cast<const DatabaseBusy*>(&error) !=
        nullptr)  // dynamic_cast returns a real pointer only when the
                  // object actually is a DatabaseBusy.
    {
      return true;  // Our own busy exception is definitive: no further
                    // checks are needed.
    }

    const std::string message =
        error.what();  // Copy the human-readable message into a string so
                       // we can search it safely; what() comes from
                       // std::exception.
    // The substrings cover pool-acquisition timeouts and the common
    // PostgreSQL "too many clients" error. The search is case-sensitive, so
    // the words must appear exactly as written here.
    return message.find("timeout") != std::string::npos ||
           message.find("pool") !=
               std::string::npos ||  // npos means "not found"; finding any
                                     // one word is enough to call the error
                                     // busy.
           message.find("too many clients") != std::string::npos ||
           message.find("connection") !=
               std::string::npos;  // The last two catch PostgreSQL errors
                                   // and generic connection failures;
                                   // "connection" is deliberately broad.
  }

  drogon::HttpStatusCode toHttpStatus(const std::exception& error)
  {
    // Busy is a retryable condition, so it is the only case answered with
    // 503.
    if (isBusy(error))  // Ask the shared classifier instead of repeating
                        // the string checks here.
    {
      return drogon::k503ServiceUnavailable;  // 503 tells the caller
                                              // "service temporarily
                                              // unavailable, try again
                                              // later".
    }
    return drogon::k500InternalServerError;  // Anything else is treated as
                                             // an unexpected internal
                                             // failure (500).
  }

  void throwIfBusy(const std::exception& error)
  {
    // Re-classify at the boundary so the HTTP layer can keep its single
    // DatabaseBusy handling path.
    if (isBusy(error))  // Only busy errors are re-thrown; other failures
                        // are left for the caller to handle.
    {
      throw DatabaseBusy();  // Throwing unwinds the coroutine so the
                             // existing 503 + Retry-After handler takes
                             // over.
    }
  }
}  // namespace asyncdb
