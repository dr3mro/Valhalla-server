/**
 * @file errorstatus.hpp
 * @brief Maps caught exceptions to the HTTP status reported to API clients.
 * @details Provides one small helper, statusForException, that looks at an
 *          exception which was caught and decides which HTTP status code the
 *          client should receive. A "database busy" problem maps to 503 (try
 *          again later); every other problem maps to 500 (generic server
 * error). The function is inline and header-only, so there is no separate .cpp
 *          file and no linking needed.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * When a controller fails while handling a request it must still send an HTTP
 * answer. The CRITICALMESSAGERESPONSE macro (in global.hpp) calls this helper
 * to choose that answer's status. Doing the choice in one place keeps every
 * error response consistent: clients always get 503 specifically when the
 * database was too busy, and 500 for anything else. Without it, each catch
 * block would have to repeat the classification logic.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Exceptions in C++: https://en.cppreference.com/w/cpp/error/exception
 * - dynamic_cast: https://en.cppreference.com/w/cpp/language/dynamic_cast
 * - inline functions: https://en.cppreference.com/w/cpp/language/inline
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per translation unit.

// Provides std::exception, the base type handled by statusForException.
#include <exception>

// The DatabaseBusy exception type that triggers the 503 case.
#include "database/databasebusy.hpp"
// Provides the Status enum with the 500/503 values returned here.
#include "utils/global/http.hpp"

namespace api::v2::Http  // Same namespace as the Status enum, so the helper
                         // sits beside the codes it returns.
{
  // 503 when no database connection was available (the client should retry),
  // 500 for any other failure.
  /**
   * @brief Selects the HTTP status corresponding to a caught exception.
   * @details A DatabaseBusy means no database connection was available, so
   * the client is told 503 and may retry; all other failures are 500. Used by
   * the CRITICALMESSAGERESPONSE macro.
   * @param[in] e The caught exception to classify.
   * @return Status::SERVICE_UNAVAILABLE for DatabaseBusy, otherwise
   *         Status::INTERNAL_SERVER_ERROR.
   * @note Classification uses dynamic_cast, so it relies on the exception
   *       being polymorphic (as all std::exception-derived types are).
   */
  inline Status statusForException(
      const std::exception& e)  // Takes the exception by const reference: no
                                // copy, and it is only inspected.
  {
    // dynamic_cast checks at runtime whether e is really a DatabaseBusy; if
    // so, "retry later" (503), else generic 500.
    return dynamic_cast<const DatabaseBusy*>(&e) != nullptr
               ? Status::SERVICE_UNAVAILABLE
               : Status::INTERNAL_SERVER_ERROR;  // 503 if it is a
                                                 // DatabaseBusy, otherwise
                                                 // 500, so the caller can
                                                 // choose the right reply.
  }
}  // namespace api::v2::Http
