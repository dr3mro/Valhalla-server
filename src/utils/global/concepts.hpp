/**
 * @file concepts.hpp
 * @brief Compile-time constraints tying generic API code to domain types.
 * @details Declares four C++20 "concepts" — named conditions that a type must
 *          satisfy before a template will accept it. Each one checks that a
 * type is a particular domain class (Client, Service, Case or Appointment) or
 * something derived from it. The classes themselves are only forward-declared
 * here, so this header stays light and does not pull in heavy definitions. The
 * checks happen during compilation, so a wrong type produces a clear compile
 * error instead of a crash at runtime.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * The controllers are templates that work with several related entity types.
 * These concepts act as a safety gate: they stop someone instantiating a
 * controller with an unrelated type (for example, a std::string) by rejecting
 * it at compile time. This keeps the generic API code flexible but still tied
 * to the real domain objects. It is a header-only helper included where those
 * generic templates are declared.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - C++20 concepts / constraints:
 * https://en.cppreference.com/w/cpp/language/constraints
 * - std::is_base_of: https://en.cppreference.com/w/cpp/types/is_base_of
 * - Forward declarations:
 * https://en.cppreference.com/w/cpp/language/class#Forward_declaration
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this file at most once per translation unit.

// Provides std::is_base_of_v used by each concept below.
// The nested namespace api::v2 groups the version-2 API
// code.
#include <type_traits>
namespace api::v2 {
  class Client;  // Forward declaration: "this class exists elsewhere", so we
                 // can refer to it without including its full definition.
}  // namespace api::v2

class Service;  // Forward declaration of the Service domain class (a bookable
                // service).
class Case;  // Forward declaration of the Case domain class (a patient case).
class Appointment;  // Forward declaration of the Appointment domain class (a
                    // booked slot).

/**
 * @concept Client_t
 * @brief Constrains `T` to api::v2::Client or a type derived from it.
 * @tparam T Type to test.
 * @see Client
 */
template <typename T>  // T is the type being checked when a template is used.
concept Client_t =
    std::is_base_of_v<api::v2::Client,
                      T>;  // True only if T is Client or inherits from it.

/**
 * @concept Service_t
 * @brief Constrains `T` to Service or a type derived from it.
 * @tparam T Type to test.
 * @see Service
 */
template <typename T>  // T is the type being checked.
concept Service_t =
    std::is_base_of_v<Service,
                      T>;  // True only if T is Service or a subclass of it.

/**
 * @concept Case_t
 * @brief Constrains `T` to Case or a type derived from it.
 * @tparam T Type to test.
 * @see Case
 */
template <typename T>  // T is the type being checked.
concept Case_t =
    std::is_base_of_v<Case, T>;  // True only if T is Case or a subclass of it.

/**
 * @concept Appointment_t
 * @brief Constrains `T` to Appointment or a type derived from it.
 * @tparam T Type to test.
 * @see Appointment
 */
template <typename T>  // T is the type being checked.
concept Appointment_t =
    std::is_base_of_v<Appointment,
                      T>;  // True only if T is Appointment or a subclass of it.
