/**
 * @file appointmentcontroller.hpp
 * @brief Appointment-entity controller that adapts @ref EntityController to
 * @ref AppointmentControllerBase.
 * @details In plain words, this file defines a small "adapter" class for each
 * kind of appointment (clinic appointment, pharmacy appointment, laboratory
 * appointment, radiology-center appointment). An adapter is a thin wrapper that
 * passes a request straight through to another class that does the real work —
 * here @ref EntityController. Every job below (create, read, update, delete,
 * search and their coroutine versions) simply forwards to the matching method
 * of `EntityController<T>`. This class adds no behaviour of its own:
 *          validation, permission checks, building the database query, turning
 * results into JSON and mapping errors to HTTP codes all happen in that base
 * class.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It gives appointment entities their own interface, separate from service
 * entities, so the route registry can pick the right controller family by name.
 * The HTTP layer calls these methods through
 * @ref AppointmentControllerBase (using a `std::variant`), and each call lands
 * here and is handed to
 * @ref EntityController. If this file were missing, there would be no concrete
 * class satisfying the appointment interface and no way to route an appointment
 * request to the real CRUD+S code.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Templates (writing one class that works for many types):
 * https://en.cppreference.com/w/cpp/language/templates
 * - C++20 concepts / requires (compile-time checks on template arguments):
 * https://en.cppreference.com/w/cpp/language/constraints
 * - C++20 coroutines (functions that pause and resume with co_await/co_return):
 * https://en.cppreference.com/w/cpp/language/coroutines
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this file at most once per source file, so its
              // declarations are never duplicated.

// Gives fixed-width whole-number types such as uint64_t (an unsigned 64-bit
// number).
#include <cstdint>
// Gives std::optional: a box that either holds a value or says "there is no
// value".
#include <optional>
// Gives std::string_view: a cheap, non-owning view over a stretch of text.
#include <string_view>

// The interface (pure virtual jobs) that this class implements.
#include "controllers/appointmentcontroller/appointmentcontrollerbase.hpp"
// The base class that does the actual CRUD+S work this class forwards to.
#include "controllers/entitycontroller/entitycontroller.hpp"
// Defines CALLBACK_, the "call me when finished" function type passed to every
// operation.
#include "utils/global/callback.hpp"
// Defines the Appointment_t concept used to constrain which types may be used
// as T.
#include "utils/global/concepts.hpp"

/**
 * @class AppointmentController
 * @brief Appointment-entity controller that adapts @ref EntityController to
 * @ref AppointmentControllerBase.
 * @tparam T Appointment entity type; must satisfy @ref Appointment_t (derived
 * from `Appointment`).
 * @details A thin, stateless adapter: each CRUD+S entry point forwards to the
 * same-named method of `EntityController<T>`. Schema validation, permission
 * checks, entity construction, SQL execution, JSON marshalling and error→HTTP
 * mapping are therefore inherited rather than reimplemented here; `T` supplies
 * the table name, SQL statements and permission queries. The distinct base
 * interface lets the appointments route registry select appointment controllers
 * independently of service controllers.
 *
 *          Plain-language version: `T` is whichever appointment class we are
 * handling (say `ClinicAppointment`). The `Appointment_t` after `template` is a
 * "concept" — a compile-time check that refuses to build this class unless `T`
 * is really an appointment type. Because this class stores no data
 * ("stateless"), its only job is to hand the arguments to `EntityController<T>`
 * and let that base do everything.
 *
 *          Explicit instantiations exist for `ClinicAppointment`,
 * `PharmacyAppointment`, `LaboratoryAppointment` and
 * `RadiologyCenterAppointment`.
 * @note Copy and move are permitted, matching the value semantics of @ref
 * EntityController; the route registry nonetheless holds controllers as shared
 * pointers.
 * @see EntityController, AppointmentControllerBase, Appointment_t
 */
template <Appointment_t T>  // "template" means T is chosen by the caller;
                            // "Appointment_t" only allows appointment types.
class AppointmentController
    : public EntityController<T>,
      public AppointmentControllerBase  // Inherits real CRUD+S work from
                                        // EntityController<T> and the
                                        // appointment interface from
                                        // AppointmentControllerBase.
{
 public:
  /** @brief Default construction; dependencies are owned by @ref
   * EntityController. */
  explicit AppointmentController() =
      default;  // "= default" uses the compiler-generated constructor;
                // nothing extra to set up.
  /** @brief Move construction is allowed. */
  AppointmentController(AppointmentController&&) =
      default;  // Moving is allowed and simply moves the base's state.
  /** @brief Copy construction is allowed. */
  AppointmentController(const AppointmentController&) =
      default;  // Copying is allowed, matching EntityController's value-style
                // behaviour.
  /** @brief Move assignment is allowed. */
  AppointmentController& operator=(AppointmentController&&) =
      default;  // Move-assignment is likewise allowed.
  /** @brief Copy assignment is allowed. */
  AppointmentController& operator=(const AppointmentController&) =
      default;  // Copy-assignment is allowed too.
  /** @brief Defaulted destructor; no resources are owned directly. */
  ~AppointmentController() override =
      default;  // "override" confirms this matches the base's virtual
                // destructor; nothing to clean up.

  // CRUDS — short label meaning Create-Read-Update-Delete-Search, the jobs
  // forwarded below.
  /** @brief Forwards to `EntityController<T>::Create`; see that method for
   * validation and error codes. */
  void Create(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view data) override;  // "override" means this implements the
                                        // base interface's pure virtual Create.
  /** @brief Coroutine form of @ref Create; co_awaits
   * `EntityController<T>::CreateAsync`. */
  drogon::Task<void> CreateAsync(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data) override;  // Returns a pausable task; the real
                                         // async work is in the base.
  /** @brief Forwards to `EntityController<T>::Read`; `data` must contain `id`
   * and `schema`. */
  void Read(CALLBACK_&&       callback,
            const Requester&& requester,
            std::string_view data) override;  // Just a passthrough; the base
                                              // decides what to do with `data`.
  /** @brief Coroutine form of @ref Read; co_awaits
   * `EntityController<T>::ReadAsync`. */
  drogon::Task<void> ReadAsync(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data) override;  // Pausable version of Read.
  /** @brief Forwards to `EntityController<T>::Update`; a missing @p _id is
   * rejected there. */
  void Update(CALLBACK_&&             callback,
              const Requester&&       requester,
              std::string_view        data,
              std::optional<uint64_t> _id)
      override;  // `_id` might hold "no value"; the base decides if that is
                 // an error.
  /** @brief Coroutine form of @ref Update; co_awaits
   * `EntityController<T>::UpdateAsync`. */
  drogon::Task<void> UpdateAsync(
      CALLBACK_&&             callback,
      const Requester&&       requester,
      std::string_view        data,
      std::optional<uint64_t> _id) override;  // Pausable version of Update.
  /** @brief Forwards to `EntityController<T>::Delete`; a missing @p _id is
   * rejected there. */
  void Delete(CALLBACK_&&             callback,
              const Requester&&       requester,
              std::optional<uint64_t> _id)
      override;  // Delete needs only the target id, not a JSON body.
  /** @brief Coroutine form of @ref Delete; co_awaits
   * `EntityController<T>::DeleteAsync`. */
  drogon::Task<void> DeleteAsync(
      CALLBACK_&&             callback,
      const Requester&&       requester,
      std::optional<uint64_t> _id) override;  // Pausable version of Delete.
  /** @brief Forwards to `EntityController<T>::Search`; no separate coroutine
   * override is provided. */
  void Search(CALLBACK_&&       callback,
              const Requester&& requester,
              std::string_view  data)
      override;  // Search is synchronous only; there is no SearchAsync here.
};
