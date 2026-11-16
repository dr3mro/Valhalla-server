/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file callback.hpp
 * @brief Shared completion-callback type for the API and its workers.
 * @details Defines one macro, CALLBACK_, that names a single function-signature
 *          type used everywhere a piece of work needs to report its result when
 *          it finishes. The signature carries an integer status code and a text
 *          body, which matches how the REST API answers requests. Because it is
 *          a macro (a textual shortcut handled by the preprocessor), every file
 *          that uses it gets the exact same type without repeating the long
 *          definition.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the small glue that lets the HTTP layer and the background workers
 * agree on how to talk back. A route handler passes a CALLBACK_ into a
 * controller; the controller queues work on an executor and later "calls back"
 * with a status (see api::v2::Http::Status) and a JSON string. Without this
 * shared type, each layer would invent its own signature and the pieces would
 * no longer fit together. It is included by controllers, routes and helpers
 * across the API layer.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - std::function:
 * https://en.cppreference.com/w/cpp/utility/functional/function
 * - Lambda expressions: https://en.cppreference.com/w/cpp/language/lambda
 * - Preprocessor macros: https://en.cppreference.com/w/cpp/preprocessor/replace
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// This include is kept for the std::function used inside the CALLBACK_ macro.
#include <functional>  //IWYU pragma: keep
#pragma once  // Tells the compiler to include this file at most once per
              // translation unit (cheaper than an include guard).
/**
 * @def CALLBACK_
 * @brief Completion callback used throughout the request pipeline.
 * @details Expands to `std::function<void(int, const std::string&)>`: the first
 *          argument is an HTTP-style status code (see api::v2::Http::Status),
 *          the second is the response body, normally JSON text. Callers copy or
 *          move callbacks into lambdas queued on executors, so the callable and
 *          anything it captures must remain valid after the initiating call
 * returns.
 * @note A macro keeps the signature identical across translation units;
 *       `<functional>` is included here for the expansion.
 */
#define CALLBACK_         \
  std::function<void(int, \
      const std::string&)>  // The one shared "report your result" signature:
                            // (status code, response text) -> void.
