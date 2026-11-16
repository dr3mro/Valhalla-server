/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file global.hpp
 * @brief Macros for uniform critical-error logging and response generation.
 * @details Defines two preprocessor macros. CRITICALMESSAGE builds a detailed
 *          error string (message, function, file and line) and logs it at
 * critical severity. CRITICALMESSAGERESPONSE does that logging and then answers
 *          the caller through a callback, choosing 503 for a busy database and
 *          500 for anything else. Because they are macros, the error text picks
 * up the caller's __func__, __FILE__ and __LINE__ automatically instead of
 *          always pointing here.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Many controllers and entities can fail in the same way, and each one must
 * both log the failure and send a sensible HTTP reply. These macros make that a
 * single line at every catch site, so error handling looks and behaves the same
 * across the whole API. They depend on statusForException (errorstatus.hpp) for
 * the status code and on Message for the log. Without them, error handling
 * would be duplicated and would drift apart over time.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Preprocessor macros: https://en.cppreference.com/w/cpp/preprocessor/replace
 * - std::string: https://en.cppreference.com/w/cpp/string/basic_string
 * - Compile-time macros __func__, __FILE__, __LINE__:
 * https://en.cppreference.com/w/cpp/preprocessor/replace#Predefined_macros
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Provides statusForException used to map the caught error to a 500/503 status.
#include "utils/global/errorstatus.hpp"

/**
 * @def CRITICALMESSAGE
 * @brief Logs the active exception together with function, file and line.
 * @details Expands to several statements that build `critical_error` via fmt
 *          and pass it to Message::CriticalMessage.
 * @pre Must be expanded inside a catch handler with an exception variable named
 *      `e` in scope, and with fmt and Message available.
 * @warning Declares a local variable named `critical_error`; avoid using the
 *          macro where that name is already taken. It expands to more than one
 *          statement, so guard it with braces when used as a single statement.
 */
// NOTE: the macro lines below are kept byte-for-byte; each backslash continues
// the macro onto the next line, so a trailing // comment there would break it.
// The expansion first builds "Failure: <what> : <function> : <file> : <line>"
// and then sends that string to Message::CriticalMessage for logging.
#define CRITICALMESSAGE                                                  \
  std::string critical_error = fmt::format("Failure: {} : {} : {} : {}", \
                                           e.what(),                     \
                                           __func__,                     \
                                           __FILE__,                     \
                                           __LINE__);                    \
  Message::CriticalMessage(critical_error);

// Answers 503 (with a short message) when the database is busy, 500 otherwise.
/**
 * @def CRITICALMESSAGERESPONSE
 * @brief Logs a critical error and replies to the caller through `callback`.
 * @details First expands CRITICALMESSAGE, then invokes `callback` with the
 *          status from statusForException. For a busy database (503) the
 *          exception's own message is returned so the client knows to retry;
 *          for any other failure (500) the detailed `critical_error` string is
 *          returned and the raw exception text is kept out of the response.
 * @pre Requires `e`, a callable `callback` compatible with CALLBACK_, and the
 *      names used by CRITICALMESSAGE to be in scope.
 * @see statusForException, CRITICALMESSAGE
 */
// NOTE: the macro body is unchanged; again the backslashes must stay at end of
// line. It logs via CRITICALMESSAGE, then calls callback(status, body), where
// the body is the raw error text for 503 but the safer formatted string for
// 500.
#define CRITICALMESSAGERESPONSE                               \
  CRITICALMESSAGE                                             \
  callback(api::v2::Http::statusForException(e),              \
           api::v2::Http::statusForException(e) ==            \
                   api::v2::Http::Status::SERVICE_UNAVAILABLE \
               ? std::string(e.what())                        \
               : critical_error);
