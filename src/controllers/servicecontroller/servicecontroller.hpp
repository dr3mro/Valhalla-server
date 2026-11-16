/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file servicecontroller.hpp
 * @brief Service-entity controller that adapts @ref EntityController to @ref
 * ServiceControllerBase.
 * @details In plain words, this file defines a small "adapter" class for each
 * kind of service entity (clinic, pharmacy, laboratory, radiology center). An
 * adapter is a thin wrapper that passes a request straight through to another
 * class that does the real work — here that class is
 *          @ref EntityController. Every job listed below (create, read, update,
 * delete, search and their coroutine versions) simply forwards to the matching
 * method of `EntityController<T>`. This class adds no behaviour of its own:
 * validation, permission checks, building the database query, turning results
 * into JSON and mapping errors to HTTP status codes all happen in that base.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It gives service entities their own interface, separate from appointment
 * entities, so the route registry can pick the right controller family by name.
 * The HTTP layer calls these methods through
 * @ref ServiceControllerBase (using a `std::variant`), and each call lands here
 * and is handed to
 * @ref EntityController. If this file were missing, there would be no concrete
 * class satisfying the service interface and no way to route a
 * clinic/pharmacy/laboratory/radiology request to the real CRUD+S code.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - C++20 concepts / requires (compile-time checks on template arguments):
 * https://en.cppreference.com/w/cpp/language/constraints
 * - Templates (writing one class that works for many types):
 * https://en.cppreference.com/w/cpp/language/templates
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

// The base class that does the actual CRUD+S work this class forwards to.
#include "controllers/entitycontroller/entitycontroller.hpp"
// The common interface of the entity controllers family.
#include "controllers/entitycontroller/entitycontrollerbase.hpp"
// The interface (pure virtual jobs) that this class implements.
#include "controllers/servicecontroller/servicecontrollerbase.hpp"
// Defines CALLBACK_, the "call me when finished" function type passed to every
// operation.
#include "utils/global/callback.hpp"
// Defines the Service_t concept used to constrain which types may be used as T.
#include "utils/global/concepts.hpp"

/**
 * @class ServiceController
 * @brief Service-entity controller that adapts @ref EntityController to @ref
 * ServiceControllerBase.
 * @tparam T Service entity type; must satisfy @ref Service_t (derived from
 * `Service`).
 * @details A thin, stateless adapter: every CRUD+S entry point simply forwards
 * to the same-named method of `EntityController<T>`, so validation,
 * authorisation, entity construction, SQL execution, JSON marshalling and
 * error→HTTP mapping all live in that base. The template parameter supplies the
 * table name, SQL statements and permission queries used there. This class
 * exists to give service entities a distinct interface (and therefore a
 *          distinct entry in the route registry) from appointment entities.
 *
 *          Plain-language version: `T` is whichever entity class we are
 * handling (say `Clinics`). The `Service_t` after `template` is a "concept" — a
 * compile-time check that refuses to build this class unless `T` is really a
 * service type. Because this class stores no data ("stateless"), its only job
 * is to hand the arguments to `EntityController<T>` and let that base do
 * everything.
 *
 *          Explicit instantiations are provided for `Clinics`, `Pharmacies`,
 * `Laboratories` and `RadiologyCenters`; the route layer stores
 * `shared_ptr<ServiceController<T>>` values in a variant and dispatches to them
 * through @ref ServiceControllerBase.
 * @note Copy and move are disabled so each registered controller has a single
 * identity.
 * @see EntityController, ServiceControllerBase, Service_t
 */
template <Service_t T>  // "template" means T is chosen by the caller;
                        // "Service_t" only allows service entity types.
class ServiceController
    : public EntityController<T>,
      public ServiceControllerBase  // Inherits the real CRUD+S work from
                                    // EntityController<T> and the service
                                    // interface from ServiceControllerBase.
{
 public:
  /** @brief Default construction; dependencies are owned by @ref
   * EntityController. */
  explicit ServiceController() =
      default;  // "= default" uses the compiler-generated constructor; there
                // is nothing extra to set up here.

  /** @brief Copy construction is disabled; controllers are shared, not
   * copied. */
  ServiceController(const ServiceController&) =
      delete;  // "= delete" forbids copying, so every registration keeps one
               // clear identity.
  /** @brief Move construction is disabled; controllers are shared, not moved.
   */
  ServiceController(ServiceController&&) =
      delete;  // Moving is also forbidden for the same
               // single-identity reason.
  /** @brief Copy assignment is disabled. */
  ServiceController& operator=(const ServiceController&) =
      delete;  // Assigning a copy is blocked too.
  /** @brief Move assignment is disabled. */
  ServiceController& operator=(ServiceController&&) =
      delete;  // Move-assignment is blocked as well.
  /** @brief Defaulted destructor; no resources are owned directly. */
  ~ServiceController() override =
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
  void Read(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data) override;  // Just a passthrough; see the base
                                         // for what happens to `data`.
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
  void Search(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data) override;  // Search is synchronous only; there
                                         // is no SearchAsync in this family.
};
