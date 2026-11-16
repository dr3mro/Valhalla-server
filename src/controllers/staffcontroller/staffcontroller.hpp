/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file staffcontroller.hpp
 * @brief Implements staff management for one service entity type.
 * @details In plain words, this file defines the class that actually carries
 * out the three staff jobs declared in @ref StaffControllerBase - adding a
 * member, removing a member, and inviting a member by email. Unlike the service
 * controllers, this class is not an @ref EntityController, it inherits @ref
 * Controller for the low-level "add/remove staff" database helpers and writes
 *          the job logic itself. For each job it reads the request, asks the
 * authorisation service (GateKeeper) whether the caller is allowed, and only
 * then does something — run SQL or send an HTTP request to the email daemon.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It is the concrete implementation behind the staff HTTP routes. The route
 * layer calls these methods through @ref StaffControllerBase (with a
 * `std::variant` choosing the entity type); each call lands here, where
 * permission is checked before any change is made. It calls into @ref
 * Controller for SQL,
 * @ref GateKeeper for permission decisions and the email daemon for
 * invitations. Without it, there would be no code to add, remove or invite
 * staff, and the staff interface would have nothing behind it.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - std::shared_ptr (a smart pointer that shares ownership of an object):
 * https://en.cppreference.com/w/cpp/memory/shared_ptr
 * - Forward declarations (telling the compiler a type exists before defining
 * it): https://en.cppreference.com/w/cpp/language/declarations
 * - C++20 coroutines (functions that pause and resume with co_await/co_return):
 * https://en.cppreference.com/w/cpp/language/coroutines
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this file at most once per source file, so its
              // declarations are never duplicated.

// Gives std::shared_ptr, the smart pointer used to hold shared dependencies.
#include <memory>
// Gives std::string_view: a cheap, non-owning view over a stretch of text.
#include <string_view>

// The base providing the low-level add/remove-staff SQL helpers.
#include "controllers/base/controller/controller.hpp"
// The interface (pure virtual jobs) that this class implements.
#include "controllers/staffcontroller/staffcontrollerBase.hpp"
// Drogon's coroutine support; defines drogon::Task, the pausable task type.
#include "drogon/utils/coroutine.h"
// Defines CALLBACK_, the "call me when finished" function type passed to every
// operation.
#include "utils/global/callback.hpp"

namespace api::v2  // Groups related classes under the "api::v2" namespace to
                   // avoid name clashes.
{
  /** @brief Authorisation service used to decide whether a requester may
   * manage a service's staff. */
  class GateKeeper;  // Forward declaration: says this class exists without
                     // needing its full definition here.
}  // namespace api::v2

/** @brief Convenience alias for the authorisation service. */
using GateKeeper =
    api::v2::GateKeeper;  // A short nickname so we can write "GateKeeper"
                          // instead of "api::v2::GateKeeper".
/** @brief Forward declaration of the owning runtime-configuration aggregate. */
class Configurator;  // Says the configuration class exists; only a pointer to
                     // it is stored, so the full definition is not needed here.

/**
 * @class StaffController
 * @brief Implements staff management for one service entity type.
 * @tparam T Service entity type (e.g. `Clinics`); no concept constraint is
 * declared, but the implementation relies on `T` constructing from @ref
 * api::v2::Types::StaffData and on `T::getSqlAddStaffStatement` /
 * `T::getSqlRemoveStaffStatement` being available through
 *           @ref Controller.
 * @details Unlike @ref ServiceController this is not an `EntityController`: it
 * inherits @ref Controller for the low-level add/remove-staff SQL helpers and
 * implements the domain operations of @ref StaffControllerBase directly. Each
 * operation parses the request body, obtains a `GateKeeper` staff-management
 * decision, and only then performs a side effect (an SQL statement, or an HTTP
 * POST to the email-sender daemon for invites).
 *
 *          Plain-language version: `T` is the concrete entity (say `Clinics`).
 * The class relies on `T` being constructible from staff data and on `T`
 * providing the add/remove staff SQL statements; those are compile-time
 * expectations rather than an enforced concept. The three jobs here do real
 * work (parse, check permission, act), unlike the pure forwarding service
 * controllers.
 *
 *          Explicit instantiations exist for `Clinics`, `Pharmacies`,
 * `Laboratories` and `RadiologyCenters`; the staff route registry stores
 * instances as shared pointers and selects one by service name.
 * @note Instances are shared across Drogon workers; the cached dependencies
 * below are populated in the constructor and are not mutated by the operations.
 * @see StaffControllerBase, Controller, GateKeeper, Configurator
 */
template <typename T>  // "template <typename T>" means T is a type chosen
                       // later, e.g. Clinics or Pharmacies.
class StaffController : public StaffControllerBase,
                        public Controller  // Implements the staff interface and
                                           // reuses Controller's SQL helpers.
{
 public:
  /**
   * @brief Resolves shared dependencies from the global service locator and
   * snapshots config.
   * @details Reads the registered @ref Configurator and @ref GateKeeper
   * instances through `Store::getObject`, then copies `EmailSenderConfig` out
   * of the Configurator so the invite path does not have to query
   * configuration per request.
   * @note Failure behaviour when a service is absent from the Store is that
   * of `Store::getObject`.
   */
  StaffController();  // Constructor: grabs the shared configuration and
                      // permission services once, at startup.
  /** @brief Copy construction is disabled so each registered controller has a
   * single identity. */
  StaffController(const StaffController&) =
      delete;  // "= delete" forbids copying; the controller is shared, not
               // duplicated.
  /** @brief Move construction is disabled. */
  StaffController(StaffController&&) =
      delete;  // Moving is also forbidden to keep one clear identity.
  /** @brief Copy assignment is disabled. */
  StaffController& operator=(const StaffController&) =
      delete;  // Assigning a copy is blocked for the same reason.
  /** @brief Move assignment is disabled. */
  StaffController& operator=(StaffController&&) =
      delete;  // Move-assignment is blocked as well.
  /** @brief Defaulted destructor; shared dependencies are released through
   * their shared pointers. */
  ~StaffController() override =
      default;  // "override" matches the base's virtual destructor; shared
                // pointers clean up automatically.

  // CRUDS — short label inherited from the other controller families; real
  // jobs here are add/remove/invite staff.
  /**
   * @brief Adds a staff member described by @p data to the service entity
   * identified in that body.
   * @details Parses `payload`/`id`, constructs @ref api::v2::Types::StaffData
   * and `T`, checks `canManageStaff`, then delegates to
   * `Controller::addStaff`. A parse failure is an exceptional 500 path; an
   * authorisation failure returns the GateKeeper error code.
   * @param[in,out] callback Completion handler, invoked once with (status,
   * body).
   * @param[in] requester Authenticated caller used for the staff-management
   * permission check.
   * @param[in] data Raw JSON body containing `id` and `payload`.
   */
  void AddStaffToEntity(CALLBACK_&&       callback,
                        const Requester&& requester,
                        std::string_view  data)
      override;  // "override" means this implements the pure virtual job from
                 // StaffControllerBase.
  /**
   * @brief Removes a staff member from a service entity.
   * @details Same request shape, authorisation and error behaviour as @ref
   * AddStaffToEntity, but delegates to `Controller::removeStaff`.
   * @param[in,out] callback Completion handler, invoked once with (status,
   * body).
   * @param[in] requester Authenticated caller used for the staff-management
   * permission check.
   * @param[in] data Raw JSON body containing `id` and `payload`.
   */
  void RemoveStaffFromEntity(CALLBACK_&&       callback,
                             const Requester&& requester,
                             std::string_view  data)
      override;  // Same rules as Add, but calls the remove-staff SQL helper.
  /**
   * @brief Invites a staff member by handing an invite message to the
   * email-sender daemon.
   * @details Parses `nominee_id` and @ref api::v2::Types::StaffData, checks
   * `canManageStaff`, builds the invite JSON and POSTs it via
   * `Communicate::sendRequest`. Reports 200 with the daemon's body on
   * delivery, 400 when invite construction or delivery fails.
   * @param[in,out] callback Completion handler, invoked once with (status,
   * body).
   * @param[in] requester Authenticated caller used for the staff-management
   * permission check.
   * @param[in] data Raw JSON body containing `nominee_id` and the invite
   * fields.
   */
  void InviteStaffToEntity(CALLBACK_&&       callback,
                           const Requester&& requester,
                           std::string_view  data)
      override;  // The invite job sends an email instead of running SQL.
  /**
   * @brief Coroutine form of @ref AddStaffToEntity.
   * @details co_awaits the async permission check and
   * `Controller::addStaffAsync`.
   * @param[in,out] callback Completion handler, invoked once with (status,
   * body).
   * @param[in] requester Authenticated caller used for the staff-management
   * permission check.
   * @param[in] data Raw JSON body; must remain valid until the task
   * completes.
   */
  drogon::Task<void> AddStaffToEntityAsync(CALLBACK_&&       callback,
                                           const Requester&& requester,
                                           std::string_view  data)
      override;  // Pausable version of Add; returns a task instead of
                 // finishing immediately.
  /**
   * @brief Coroutine form of @ref RemoveStaffFromEntity.
   * @details co_awaits the async permission check and
   * `Controller::removeStaffAsync`.
   * @param[in,out] callback Completion handler, invoked once with (status,
   * body).
   * @param[in] requester Authenticated caller used for the staff-management
   * permission check.
   * @param[in] data Raw JSON body; must remain valid until the task
   * completes.
   */
  drogon::Task<void> RemoveStaffFromEntityAsync(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data) override;  // Pausable version of Remove.
  /**
   * @brief Coroutine form of @ref InviteStaffToEntity.
   * @note The email-daemon POST is a synchronous cURL call in this variant
   * too.
   * @param[in,out] callback Completion handler, invoked once with (status,
   * body).
   * @param[in] requester Authenticated caller used for the staff-management
   * permission check.
   * @param[in] data Raw JSON body; must remain valid until the task
   * completes.
   */
  drogon::Task<void> InviteStaffToEntityAsync(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data) override;  // Pausable version of Invite; the
                                         // email send itself still blocks.

 private:
  /** @brief Retained Configurator service handle obtained at construction. */
  std::shared_ptr<Configurator>
      cfg_;  // A shared smart pointer to configuration; the trailing
             // underscore marks it as a private member.
  /** @brief Snapshot of the email-daemon endpoint/queue used to POST invites.
   */
  Configurator::EmailSenderConfig
      email_sender_daemon_config_;  // A copy of where to send invite emails,
                                    // saved once so we do not re-read config
                                    // per request.
  /** @brief Shared authorisation service consulted on every staff-management
   * request. */
  std::shared_ptr<GateKeeper>
      gateKeeper;  // Shared smart pointer to the permission service used
                   // before every staff change.
};
