/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file servicecontroller.cpp
 * @brief Definitions for @ref ServiceController and its explicit template
 * instantiations.
 * @details The adapter methods are defined here rather than in the header; the
 * instantiation macro at the bottom makes the concrete specializations linkable
 * by the route layer.
 *
 *          In plain words, this file contains the bodies of the small
 * forwarding methods that were only declared in the header. Each body just
 * passes its arguments on to @ref EntityController, which does the real work.
 * The block of macros at the end forces the compiler to build a concrete
 * version of the class for each service entity type, so the route layer can
 * link to them. This file has no logic of its own beyond that forwarding.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Without these definitions and the explicit instantiations, the route
 * registry's `shared_ptr<ServiceController<T>>` objects would be declared but
 * never actually compiled, and the program would fail to link (the compiler
 * would report "undefined reference"). This file is called by the route layer
 * through @ref ServiceControllerBase and calls into @ref EntityController for
 * every job. It is what turns the service interface into working, linkable
 * code.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Explicit template instantiation (forcing the compiler to build a concrete
 * version): https://en.cppreference.com/w/cpp/language/template_instantiation
 * - std::move / std::forward (passing values without copying):
 * https://en.cppreference.com/w/cpp/utility/move
 * - C++20 coroutines (functions that pause and resume with co_await/co_return):
 * https://en.cppreference.com/w/cpp/language/coroutines
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// The class declarations whose method bodies are defined below.
#include "controllers/servicecontroller/servicecontroller.hpp"

// Gives fixed-width whole-number types such as uint64_t (an unsigned 64-bit
// number).
#include <cstdint>
// Gives std::optional: a box that either holds a value or says "there is no
// value".
#include <optional>
// Gives std::string_view: a cheap, non-owning view over a stretch of text.
#include <string_view>
// Gives std::move and std::forward, used below to hand arguments on without
// copying.
#include <utility>

// Pulls in the related appointment interface family for consistency.
#include "controllers/appointmentcontroller/appointmentcontrollerbase.hpp"
// The base class that actually implements every forwarded job. The following
// entity headers are kept intentionally: the full type of each is needed by the
// explicit instantiations at the bottom of this file. "IWYU pragma: keep" tells
// the include-what-you-use tool not to remove them.
#include "controllers/entitycontroller/entitycontroller.hpp"
#include "entities/services/clinics/clinics.hpp"   // IWYU pragma: keep
#include "entities/services/laboratories.hpp"      // IWYU pragma: keep
#include "entities/services/pharmacies.hpp"        // IWYU pragma: keep
#include "entities/services/radiologycenters.hpp"  // IWYU pragma: keep
// Defines CALLBACK_, the "call me when finished" function type passed to every
// operation.
#include "utils/global/callback.hpp"
// Defines the Service_t concept that constrains the template parameter T.
#include "utils/global/concepts.hpp"

/**
 * @brief Creates a service entity; delegates entirely to @ref EntityController.
 * @details The callback is moved into the entity controller, which performs
 * JSON parsing, create-schema validation, permission checking, entity
 * construction and the INSERT. This adapter adds no behaviour and cannot change
 * the error→HTTP mapping.
 * @param[in,out] callback Completion handler invoked once by the delegate with
 * (status, body).
 * @param[in] requester Authenticated caller forwarded as a const rvalue for
 * authorisation.
 * @param[in] data Raw JSON body; must remain valid for the duration of the
 * call.
 */
template <Service_t T>  // One definition works for any type T that satisfies
                        // the Service_t concept.
void ServiceController<T>::Create(
    CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
  EntityController<T>::Create(std::move(callback),
      std::forward<const Requester&&>(requester),
      data);  // Hand everything to the base: std::move transfers the
              // callback, std::forward preserves the requester's exact type.
}

/**
 * @brief Coroutine form of @ref Create; co_awaits
 * `EntityController<T>::CreateAsync`.
 * @details The awaiting task owns the continuation; the forwarded callback
 * fires from the delegate's coroutine once its database work completes.
 * @param[in,out] callback Completion handler invoked once with (status, body).
 * @param[in] requester Authenticated caller forwarded for authorisation.
 * @param[in] data Raw JSON body; must remain valid until the task completes.
 */
template <Service_t T>
drogon::Task<void> ServiceController<T>::CreateAsync(
    CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
  co_await EntityController<T>::CreateAsync(std::move(callback),
      std::forward<const Requester&&>(requester),
      data);  // Pause here until the base's async create finishes; then this
              // task completes.
}
/**
 * @brief Reads a service entity; delegates entirely to @ref EntityController.
 * @details Expects `data` to carry the target `id` and the requested `schema`;
 * validation and the read-permission check are performed by the delegate.
 * @param[in,out] callback Completion handler invoked once with (status, body).
 * @param[in] requester Authenticated caller forwarded for authorisation.
 * @param[in] data Raw JSON body; must remain valid for the duration of the
 * call.
 */
template <Service_t T>
void ServiceController<T>::Read(
    CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
  EntityController<T>::Read(std::move(callback),
      std::forward<const Requester&&>(requester),
      data);  // Pass straight through to the base's Read; no extra checks
              // here.
}

/**
 * @brief Coroutine form of @ref Read; co_awaits
 * `EntityController<T>::ReadAsync`.
 * @param[in,out] callback Completion handler invoked once with (status, body).
 * @param[in] requester Authenticated caller forwarded for authorisation.
 * @param[in] data Raw JSON body; must remain valid until the task completes.
 */
template <Service_t T>
drogon::Task<void> ServiceController<T>::ReadAsync(
    CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
  co_await EntityController<T>::ReadAsync(std::move(callback),
      std::forward<const Requester&&>(requester),
      data);  // Suspends until the base's async read is done, then resumes
              // and returns.
}

/**
 * @brief Updates a service entity; delegates entirely to @ref EntityController.
 * @details A disengaged @p _id is rejected by the delegate before any database
 * work.
 * @param[in,out] callback Completion handler invoked once with (status, body).
 * @param[in] requester Authenticated caller forwarded for authorisation.
 * @param[in] data Raw JSON body with the fields to change.
 * @param[in] _id Target entity id.
 */
template <Service_t T>
void ServiceController<T>::Update(CALLBACK_&& callback,
    const Requester&& requester, std::string_view data,
    const std::optional<uint64_t> _id)
{
  EntityController<T>::Update(std::move(callback),
      std::forward<const Requester&&>(requester), data,
      _id);  // Forward the new fields and the target id; the base validates
             // that the id exists.
}

/**
 * @brief Coroutine form of @ref Update; co_awaits
 * `EntityController<T>::UpdateAsync`.
 * @param[in,out] callback Completion handler invoked once with (status, body).
 * @param[in] requester Authenticated caller forwarded for authorisation.
 * @param[in] data Raw JSON body; must remain valid until the task completes.
 * @param[in] _id Target entity id.
 */
template <Service_t T>
drogon::Task<void> ServiceController<T>::UpdateAsync(CALLBACK_&& callback,
    const Requester&& requester, std::string_view data,
    const std::optional<uint64_t> _id)
{
  co_await EntityController<T>::UpdateAsync(std::move(callback),
      std::forward<const Requester&&>(requester), data,
      _id);  // Pause until the base finishes the async update.
}

/**
 * @brief Deletes a service entity; delegates entirely to @ref EntityController.
 * @details A disengaged @p _id is rejected by the delegate before any database
 * work.
 * @param[in,out] callback Completion handler invoked once with (status, body).
 * @param[in] requester Authenticated caller forwarded for authorisation.
 * @param[in] _id Target entity id.
 */
template <Service_t T>
void ServiceController<T>::Delete(CALLBACK_&& callback,
    const Requester&& requester, const std::optional<uint64_t> _id)
{
  EntityController<T>::Delete(std::move(callback),
      std::forward<const Requester&&>(requester),
      _id);  // Forward only the target id; no JSON body is needed for a
             // delete.
}

/**
 * @brief Coroutine form of @ref Delete; co_awaits
 * `EntityController<T>::DeleteAsync`.
 * @param[in,out] callback Completion handler invoked once with (status, body).
 * @param[in] requester Authenticated caller forwarded for authorisation.
 * @param[in] _id Target entity id.
 */
template <Service_t T>
drogon::Task<void> ServiceController<T>::DeleteAsync(CALLBACK_&& callback,
    const Requester&& requester, const std::optional<uint64_t> _id)
{
  co_await EntityController<T>::DeleteAsync(std::move(callback),
      std::forward<const Requester&&>(requester),
      _id);  // Pause until the base finishes the async delete.
}

/**
 * @brief Runs a paginated search; delegates entirely to @ref EntityController.
 * @details The delegate builds the search entity from `data` and returns
 * `results`, `more` and `offset`; the requester is forwarded but not used by
 * the shipped search path.
 * @param[in,out] callback Completion handler invoked once with (status, body).
 * @param[in] requester Authenticated caller forwarded for interface symmetry.
 * @param[in] data Raw JSON search criteria; must remain valid for the duration
 * of the call.
 */
template <Service_t T>
void ServiceController<T>::Search(
    CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
  EntityController<T>::Search(std::move(callback),
      std::forward<const Requester&&>(requester),
      data);  // Forward the search criteria; the base returns
              // results/more/offset through the callback.
}

/**
 * @brief Forces compilation of all member templates for one concrete service
 * entity type.
 * @details Definitions live in this translation unit, so without these
 * instantiations the route layer's `shared_ptr<ServiceController<T>>`
 * registrations would fail to link. A "translation unit" is one source file
 * plus everything it includes after preprocessing.
 * @param TYPE Concrete service entity class to instantiate.
 */
#define INSTANTIATE_SERVICE_CONTROLLER(TYPE) /*NOLINT*/               \
  template void ServiceController<TYPE>::Create(CALLBACK_&& callback, \
      const Requester&& requester, std::string_view data);            \
  template drogon::Task<void> ServiceController<TYPE>::CreateAsync(   \
      CALLBACK_&& callback, const Requester&& requester,              \
      std::string_view data);                                         \
  template void ServiceController<TYPE>::Read(CALLBACK_&& callback,   \
      const Requester&& requester, std::string_view data);            \
  template drogon::Task<void> ServiceController<TYPE>::ReadAsync(     \
      CALLBACK_&& callback, const Requester&& requester,              \
      std::string_view data);                                         \
  template void ServiceController<TYPE>::Update(CALLBACK_&& callback, \
      const Requester&& requester, std::string_view data,             \
      std::optional<uint64_t> id);                                    \
  template drogon::Task<void> ServiceController<TYPE>::UpdateAsync(   \
      CALLBACK_&& callback, const Requester&& requester,              \
      std::string_view data, std::optional<uint64_t> id);             \
  template void ServiceController<TYPE>::Delete(CALLBACK_&& callback, \
      const Requester&& requester, std::optional<uint64_t> id);       \
  template drogon::Task<void> ServiceController<TYPE>::DeleteAsync(   \
      CALLBACK_&& callback, const Requester&& requester,              \
      std::optional<uint64_t> id);                                    \
  template void ServiceController<TYPE>::Search(CALLBACK_&& callback, \
      const Requester&& requester, std::string_view data);

// Instantiate for all entity types — each line below builds a concrete
// controller for one service type.

INSTANTIATE_SERVICE_CONTROLLER(Clinics)  // Build the clinic controller's code
                                         // so the route layer can link to it.
INSTANTIATE_SERVICE_CONTROLLER(
    Pharmacies)  // Build the pharmacy controller's code so the route layer can
                 // link to it.
INSTANTIATE_SERVICE_CONTROLLER(
    Laboratories)  // Build the laboratory controller's code so the route layer
                   // can link to it.
INSTANTIATE_SERVICE_CONTROLLER(
    RadiologyCenters)  // Build the radiology-center controller's code so the
                       // route layer can link to it.
