/**
 * @file appointmentcontroller.cpp
 * @brief Definitions for @ref AppointmentController and its explicit template
 * instantiations.
 * @details Each operation is a pure forwarder to @ref EntityController;
 * instantiations at the end of the file make the concrete specializations
 * available to the appointments route layer.
 *
 *          In plain words, this file contains the bodies of the small
 * forwarding methods that were only declared in the header. Each body just
 * passes its arguments on to @ref EntityController, which does the real work
 * (parsing, validation, permission checks, SQL and JSON output). The block of
 * macros at the end forces the compiler to build a concrete version of the
 * class for each appointment type, so the route layer can link to them. There
 * is no extra logic here.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Without these definitions and the explicit instantiations, the route
 * registry's `shared_ptr<AppointmentController<T>>` objects would be declared
 * but never actually compiled, and the program would fail to link (the compiler
 * would report "undefined reference"). This file is called by the appointments
 * route layer through @ref AppointmentControllerBase and calls into
 * @ref EntityController for every job. It turns the appointment interface into
 * working, linkable code.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Explicit template instantiation (forcing the compiler to build a concrete
 * version): https://en.cppreference.com/w/cpp/language/template_instantiation
 * - std::move (transferring ownership instead of copying):
 * https://en.cppreference.com/w/cpp/utility/move
 * - C++20 coroutines (functions that pause and resume with co_await/co_return):
 * https://en.cppreference.com/w/cpp/language/coroutines
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// The class declarations whose method bodies are defined below.
#include "controllers/appointmentcontroller/appointmentcontroller.hpp"

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

// The interface family this class belongs to.
#include "controllers/appointmentcontroller/appointmentcontrollerbase.hpp"
// The base class that actually implements every forwarded job. The following
// entity headers are kept intentionally: the full type of each is needed by the
// explicit instantiations at the bottom of this file. "IWYU pragma: keep" tells
// the include-what-you-use tool not to remove them.
#include "controllers/entitycontroller/entitycontroller.hpp"
#include "entities/appointments/clinic.hpp"           // IWYU pragma: keep
#include "entities/appointments/laboratory.hpp"       // IWYU pragma: keep
#include "entities/appointments/pharmacy.hpp"         // IWYU pragma: keep
#include "entities/appointments/radiologycenter.hpp"  // IWYU pragma: keep
// Defines CALLBACK_, the "call me when finished" function type passed to every
// operation.
#include "utils/global/callback.hpp"
// Defines the Appointment_t concept that constrains the template parameter T.
#include "utils/global/concepts.hpp"

/**
 * @brief Creates an appointment entity; delegates entirely to @ref
 * EntityController.
 * @details Parsing, schema validation, authorisation, entity construction and
 * the INSERT all happen in the delegate; this adapter only moves the callback
 * and requester through.
 * @param[in,out] callback Completion handler invoked once by the delegate with
 * (status, body).
 * @param[in] requester Authenticated caller forwarded for authorisation.
 * @param[in] data Raw JSON body; must remain valid for the duration of the
 * call.
 */
template <Appointment_t T>  // One definition works for any type T satisfying
                            // the Appointment_t concept.
void AppointmentController<T>::Create(CALLBACK_&&       callback,
                                      const Requester&& requester,
                                      std::string_view  data) {
  EntityController<T>::Create(std::move(callback),
                              std::move(requester),
                              data);  // Hand everything to the base; std::move
                                      // transfers the values so they are not
                                      // copied.
}

/**
 * @brief Coroutine form of @ref Create; co_awaits
 * `EntityController<T>::CreateAsync`.
 * @param[in,out] callback Completion handler invoked once with (status, body).
 * @param[in] requester Authenticated caller forwarded for authorisation.
 * @param[in] data Raw JSON body; must remain valid until the task completes.
 */
template <Appointment_t T>
drogon::Task<void> AppointmentController<T>::CreateAsync(
    CALLBACK_&& callback, const Requester&& requester, std::string_view data) {
  co_await EntityController<T>::CreateAsync(std::move(callback),
                                            std::move(requester),
                                            data);  // Pause until the base's
                                                    // async create finishes;
                                                    // then this task completes.
}

/**
 * @brief Reads an appointment entity; delegates entirely to @ref
 * EntityController.
 * @details Expects `data` to carry the target `id` and the requested `schema`.
 * @param[in,out] callback Completion handler invoked once with (status, body).
 * @param[in] requester Authenticated caller forwarded for authorisation.
 * @param[in] data Raw JSON body; must remain valid for the duration of the
 * call.
 */
template <Appointment_t T>
void AppointmentController<T>::Read(CALLBACK_&&       callback,
                                    const Requester&& requester,
                                    std::string_view  data) {
  EntityController<T>::Read(std::move(callback),
                            std::move(requester),
                            data);  // Pass straight through to the base's Read;
                                    // no extra checks here.
}

/**
 * @brief Coroutine form of @ref Read; co_awaits
 * `EntityController<T>::ReadAsync`.
 * @param[in,out] callback Completion handler invoked once with (status, body).
 * @param[in] requester Authenticated caller forwarded for authorisation.
 * @param[in] data Raw JSON body; must remain valid until the task completes.
 */
template <Appointment_t T>
drogon::Task<void> AppointmentController<T>::ReadAsync(
    CALLBACK_&& callback, const Requester&& requester, std::string_view data) {
  co_await EntityController<T>::ReadAsync(std::move(callback),
                                          std::move(requester),
                                          data);  // Suspends until the base's
                                                  // async read is done, then
                                                  // resumes and returns.
}

/**
 * @brief Updates an appointment entity; delegates entirely to @ref
 * EntityController.
 * @details A disengaged @p _id is rejected by the delegate before any database
 * work.
 * @param[in,out] callback Completion handler invoked once with (status, body).
 * @param[in] requester Authenticated caller forwarded for authorisation.
 * @param[in] data Raw JSON body with the fields to change.
 * @param[in] _id Target entity id.
 */
template <Appointment_t T>
void AppointmentController<T>::Update(CALLBACK_&&                   callback,
                                      const Requester&&             requester,
                                      std::string_view              data,
                                      const std::optional<uint64_t> _id) {
  EntityController<T>::Update(std::move(callback),
                              std::move(requester),
                              data,
                              _id);  // Forward the new fields and target id;
                                     // the base validates the id.
}

/**
 * @brief Coroutine form of @ref Update; co_awaits
 * `EntityController<T>::UpdateAsync`.
 * @param[in,out] callback Completion handler invoked once with (status, body).
 * @param[in] requester Authenticated caller forwarded for authorisation.
 * @param[in] data Raw JSON body; must remain valid until the task completes.
 * @param[in] _id Target entity id.
 */
template <Appointment_t T>
drogon::Task<void> AppointmentController<T>::UpdateAsync(
    CALLBACK_&&                   callback,
    const Requester&&             requester,
    std::string_view              data,
    const std::optional<uint64_t> _id) {
  co_await EntityController<T>::UpdateAsync(std::move(callback),
                                            std::move(requester),
                                            data,
                                            _id);  // Pause until the base
                                                   // finishes the async update.
}

/**
 * @brief Deletes an appointment entity; delegates entirely to @ref
 * EntityController.
 * @details A disengaged @p _id is rejected by the delegate before any database
 * work.
 * @param[in,out] callback Completion handler invoked once with (status, body).
 * @param[in] requester Authenticated caller forwarded for authorisation.
 * @param[in] _id Target entity id.
 */
template <Appointment_t T>
void AppointmentController<T>::Delete(CALLBACK_&&                   callback,
                                      const Requester&&             requester,
                                      const std::optional<uint64_t> _id) {
  EntityController<T>::Delete(std::move(callback),
                              std::move(requester),
                              _id);  // Forward only the target id; no JSON body
                                     // is needed for a delete.
}

/**
 * @brief Coroutine form of @ref Delete; co_awaits
 * `EntityController<T>::DeleteAsync`.
 * @param[in,out] callback Completion handler invoked once with (status, body).
 * @param[in] requester Authenticated caller forwarded for authorisation.
 * @param[in] _id Target entity id.
 */
template <Appointment_t T>
drogon::Task<void> AppointmentController<T>::DeleteAsync(
    CALLBACK_&&                   callback,
    const Requester&&             requester,
    const std::optional<uint64_t> _id) {
  co_await EntityController<T>::DeleteAsync(std::move(callback),
                                            std::move(requester),
                                            _id);  // Pause until the base
                                                   // finishes the async delete.
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
template <Appointment_t T>
void AppointmentController<T>::Search(CALLBACK_&&       callback,
                                      const Requester&& requester,
                                      std::string_view  data) {
  EntityController<T>::Search(std::move(callback),
                              std::move(requester),
                              data);  // Forward the criteria; the base returns
                                      // results/more/offset through the
                                      // callback.
}

/**
 * @brief Forces compilation of all member templates for one concrete
 * appointment entity type.
 * @details Definitions live in this translation unit, so without these
 * instantiations the route layer's `shared_ptr<AppointmentController<T>>`
 * registrations would fail to link. A "translation unit" is one source file
 * plus everything it includes after preprocessing.
 * @param TYPE Concrete appointment entity class to instantiate.
 */
#define INSTANTIATE_APPOINTMENT_CONTROLLER(                                    \
    TYPE) /* NOLINT ( -readability-macro-expansion) */                         \
  template void                                                                \
  AppointmentController<TYPE>::Create(CALLBACK_&&       callback,              \
                                      const Requester&& requester,             \
                                      std::string_view  data);                 \
  template drogon::Task<void>                                                  \
  AppointmentController<TYPE>::CreateAsync(CALLBACK_&&       callback,         \
                                           const Requester&& requester,        \
                                           std::string_view  data);            \
  template void AppointmentController<TYPE>::Read(CALLBACK_&&       callback,  \
                                                  const Requester&& requester, \
                                                  std::string_view  data);     \
  template drogon::Task<void>                                                  \
  AppointmentController<TYPE>::ReadAsync(CALLBACK_&&       callback,           \
                                         const Requester&& requester,          \
                                         std::string_view  data);              \
  template void                                                                \
  AppointmentController<TYPE>::Update(CALLBACK_&&             callback,        \
                                      const Requester&&       requester,       \
                                      std::string_view        data,            \
                                      std::optional<uint64_t> _id);            \
  template drogon::Task<void>                                                  \
  AppointmentController<TYPE>::UpdateAsync(CALLBACK_&&             callback,   \
                                           const Requester&&       requester,  \
                                           std::string_view        data,       \
                                           std::optional<uint64_t> _id);       \
  template void                                                                \
  AppointmentController<TYPE>::Delete(CALLBACK_&&             callback,        \
                                      const Requester&&       requester,       \
                                      std::optional<uint64_t> _id);            \
  template drogon::Task<void>                                                  \
  AppointmentController<TYPE>::DeleteAsync(CALLBACK_&&             callback,   \
                                           const Requester&&       requester,  \
                                           std::optional<uint64_t> _id);       \
  template void                                                                \
  AppointmentController<TYPE>::Search(CALLBACK_&&       callback,              \
                                      const Requester&& requester,             \
                                      std::string_view  data);

INSTANTIATE_APPOINTMENT_CONTROLLER(
    ClinicAppointment)  // Build the clinic-appointment controller's code so the
                        // route layer can link to it.
INSTANTIATE_APPOINTMENT_CONTROLLER(
    PharmacyAppointment)  // Build the pharmacy-appointment controller's code so
                          // the route layer can link to it.
INSTANTIATE_APPOINTMENT_CONTROLLER(
    LaboratoryAppointment)  // Build the laboratory-appointment controller's
                            // code so the route layer can link to it.
INSTANTIATE_APPOINTMENT_CONTROLLER(
    RadiologyCenterAppointment)  // Build the radiology-center-appointment
                                 // controller's code so the route layer can
                                 // link to it.
