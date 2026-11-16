/**
 * @file databasebusy.hpp
 * @brief Exception signalling that the connection pool could not lease a
 * connection in time.
 * @details This header defines DatabaseBusy, a tiny exception class used as a
 * signal. It adds no data of its own; it simply derives from std::runtime_error
 * with a default message so it can be caught either specifically or by generic
 * error handlers. Its whole purpose is to give "the pool is exhausted" a
 * distinct type.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * The HTTP layer needs to tell two very different failures apart: a broken
 * query (probably a server bug, answer 500) and a temporary shortage of
 * connections (the client should retry, answer 503 with Retry-After).
 * DatabaseConnectionPool::get_connection() throws this type when its wait
 * expires, and filters, controllers and the watchdog all catch it separately.
 * Without a dedicated type, every timeout would look like a generic error and
 * clients would not know to retry.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Exceptions and catch blocks:
 * https://en.cppreference.com/w/cpp/language/try_catch
 * - std::runtime_error (a standard exception carrying a message):
 * https://en.cppreference.com/w/cpp/error/runtime_error
 * - Inheritance: https://en.cppreference.com/w/cpp/language/derived_class
 * - HTTP 503 Service Unavailable and Retry-After
 * - explicit constructors (prevent accidental implicit conversion):
 * https://en.cppreference.com/w/cpp/language/explicit
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include guard: define this exception at most once per
              // translation unit.

#include <stdexcept>  // std::runtime_error, the base class, is declared here.
#include <string>     // std::string is used for the exception message.

// Thrown when no database connection became free within DB_ACQUIRE_TIMEOUT_MS.
// It is not a query failure: callers answer 503 with Retry-After instead of a
// generic error.
/**
 * @class DatabaseBusy
 * @brief Exception signalling that the connection pool could not lease a
 * connection in time.
 * @details Distinguishes pool exhaustion from a genuine query failure so the
 * HTTP layer can map it to 503 (Service Unavailable) with Retry-After rather
 * than 500. Derives from `std::runtime_error` so generic handlers still catch
 * it.
 * @see api::v2::Http::statusForException,
 * DatabaseConnectionPool::get_connection
 */
class DatabaseBusy
    : public std::runtime_error  // Inherits the message-carrying behaviour and
                                 // stays catchable as a std::exception.
{
 public:
  /**
   * @brief Constructs the exception with a default or custom message.
   * @param[in] what Message describing the busy condition.
   */
  explicit DatabaseBusy(
      const std::string& what = "Database is busy, no connection available")
      : std::runtime_error(what)
  {
  }  // `explicit` prevents accidentally creating this exception from a bare
     // string; the default message explains the common case.
};
