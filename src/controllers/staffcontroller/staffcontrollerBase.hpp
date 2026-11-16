/**
 * @file staffcontrollerBase.hpp
 * @brief Type-erased interface for service-staff management operations.
 * @details In plain words, this file lists the three jobs that deal with the
 * people who work for a service entity (a clinic, pharmacy, etc.): Add a staff
 * member, Remove a staff member, and Invite a staff member by email.
 * "Type-erased" means the rest of the server can call these jobs without
 * knowing which exact kind of service is behind them. Each job appears twice:
 * an ordinary version that reports its result through a callback (a "call me
 * when you are done" function), and an "async" version that uses a coroutine (a
 * function that can pause and resume later). This file only declares the jobs;
 * the real work lives in the class that inherits from it.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the doorway the staff HTTP routes use to reach the concrete staff
 * controller for any service type. Because the functions here are "virtual",
 * one route handler can serve clinics, pharmacies, laboratories and radiology
 * centers alike — the call is automatically forwarded to the right object
 * (selected with a `std::variant`, a variable that can hold one of several
 * types). This file calls into nothing itself, but it is the contract that @ref
 * StaffController implements and that the route registry depends on. Without
 * it, managing staff would need separate route code for each entity type.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Virtual functions (calling the right version through a base pointer):
 * https://en.cppreference.com/w/cpp/language/virtual
 * - C++20 coroutines (functions that pause and resume with co_await/co_return):
 * https://en.cppreference.com/w/cpp/language/coroutines
 * - std::variant (a type-safe union holding one of several alternatives):
 * https://en.cppreference.com/w/cpp/utility/variant
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this file at most once per source file, so its
              // declarations are never duplicated.

// Gives std::string_view: a cheap, non-owning view over a stretch of text.
#include <string_view>

// Drogon's coroutine support; defines drogon::Task, the pausable task type.
#include "drogon/utils/coroutine.h"
// Defines CALLBACK_, the "call me when finished" function type passed to every
// operation.
#include "utils/global/callback.hpp"
// Defines api::v2::Requester: who is making the request (used for permission
// checks).
#include "utils/global/requester.hpp"
using Requester =
    api::v2::Requester;  // A short nickname so we can write "Requester" instead
                         // of the longer "api::v2::Requester".

/**
 * @class StaffControllerBase
 * @brief Type-erased interface for service-staff management operations.
 * @details Exposes three domain operations — add, remove and invite — each in a
 * synchronous callback form and a coroutine (`drogon::Task<void>`) form. The
 * staff route registry holds concrete controllers behind this base and
 * dispatches to them via a `std::variant` over the service entity type.
 *
 *          In plain words: "synchronous" means the caller gets the answer
 * through a callback function, while the coroutine (`drogon::Task<void>`)
 * versions can be paused with co_await so the server can do other work
 * meanwhile. "Virtual dispatch" means a call made through this base
 * automatically runs the real object's version. A `std::variant` is like a box
 * that can hold one of several different controller types at a time.
 *
 *          Invocation contract shared by every method:
 *          - `callback` is `std::function<void(int, const std::string&)>`, it
 * is moved through the call chain and, by contract, invoked once on each
 * completion path, including failures. Its first argument is an @ref
 * api::v2::Http::Status code, the second a response body.
 *          - `requester` is the authenticated caller; implementations require
 * permission to manage staff of the target service before performing any side
 * effect.
 *          - `data` is a non-owning view of the raw JSON request body, valid
 * only for the call; it must carry the target service id (`id` or `nominee_id`)
 * used for authorisation.
 *
 *          The `*Async` methods must be co_awaited; the HTTP layer wraps them
 * in `drogon::async_run` and builds the reply from the callback result.
 * @note The interface itself is stateless; behaviour lives in @ref
 * StaffController.
 * @see StaffController, Controller, GateKeeper
 */
class StaffControllerBase {
 public:
  /** @brief Default construction; the interface holds no state of its own. */
  explicit StaffControllerBase() =
      default;  // "explicit" blocks surprise implicit conversions; "=
                // default" lets the compiler write this constructor.
  /** @brief Copy construction is allowed; copies share no operation state. */
  StaffControllerBase(const StaffControllerBase&) =
      default;  // Copying is harmless here because the interface stores no
                // data.
  /** @brief Move construction is disabled; the interface is used through
   * pointers/references. */
  StaffControllerBase(StaffControllerBase&&) =
      delete;  // "= delete" makes any attempt to move
               // this interface a compile-time error.
  /** @brief Copy assignment is allowed. */
  StaffControllerBase& operator=(const StaffControllerBase&) =
      default;  // Copy-assignment is fine for the same "no state" reason.
  /** @brief Move assignment is disabled, mirroring the deleted move
   * constructor. */
  StaffControllerBase& operator=(StaffControllerBase&&) =
      delete;  // Move-assignment is blocked to match
               // the deleted move constructor.
  /** @brief Virtual destructor so derived controllers can be released through
   * this interface. */
  virtual ~StaffControllerBase() =
      default;  // "virtual" ensures deleting through this base pointer still
                // runs the derived class's cleanup.
  // CRUDS — short label copied from the other controller families; the real
  // jobs here are add/remove/invite staff.
  /**
   * @brief Adds a staff member to a service entity.
   * @details Implementations parse a `payload` holding @ref
   * api::v2::Types::StaffData plus an `id` identifying the service, check
   * staff-management permission, then execute the entity's add-staff
   * statement. A malformed body is an exceptional (500) path here, not a 400
   * validation response.
   * @param[in,out] callback Completion handler, invoked once with (status,
   * body).
   * @param[in] requester Authenticated caller used for the staff-management
   * permission check.
   * @param[in] data Raw JSON body with `id` and `payload`.
   */
  virtual void AddStaffToEntity(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view
          data) = 0;  // "= 0" = pure virtual: derived classes MUST implement
                      // this; "virtual" enables base-pointer dispatch.
  /**
   * @brief Removes a staff member from a service entity.
   * @details Same request shape and authorisation contract as @ref
   * AddStaffToEntity.
   * @param[in,out] callback Completion handler, invoked once with (status,
   * body).
   * @param[in] requester Authenticated caller used for the staff-management
   * permission check.
   * @param[in] data Raw JSON body with `id` and `payload`.
   */
  virtual void RemoveStaffFromEntity(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view
          data) = 0;  // Pure virtual remove job; same callback/permission
                      // rules as AddStaffToEntity.
  /**
   * @brief Invites a staff member to a service entity by delegating delivery
   * to the email daemon.
   * @details Implementations parse a `nominee_id` and the invite fields,
   * check permission, build an invite message and POST it to the configured
   * email-sender daemon. A 200 response echoes the daemon's body; malformed
   * invite data or a failed send yields 400.
   * @param[in,out] callback Completion handler, invoked once with (status,
   * body).
   * @param[in] requester Authenticated caller used for the staff-management
   * permission check.
   * @param[in] data Raw JSON body with `nominee_id` and the @ref
   * api::v2::Types::StaffData fields.
   */
  virtual void InviteStaffToEntity(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view data) = 0;  // Pure virtual invite job; unlike the others
                                   // it sends an email rather than running SQL.
  /**
   * @brief Coroutine form of @ref AddStaffToEntity.
   * @param[in,out] callback Completion handler, invoked once with (status,
   * body).
   * @param[in] requester Authenticated caller used for the staff-management
   * permission check.
   * @param[in] data Raw JSON body; must remain valid until the task
   * completes.
   */
  virtual drogon::Task<void> AddStaffToEntityAsync(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data) = 0;  // Same job as AddStaffToEntity, but as a
                                    // pausable task to be used with co_await.
  /**
   * @brief Coroutine form of @ref RemoveStaffFromEntity.
   * @param[in,out] callback Completion handler, invoked once with (status,
   * body).
   * @param[in] requester Authenticated caller used for the staff-management
   * permission check.
   * @param[in] data Raw JSON body; must remain valid until the task
   * completes.
   */
  virtual drogon::Task<void> RemoveStaffFromEntityAsync(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view data) = 0;  // Pausable version of RemoveStaffFromEntity.
  /**
   * @brief Coroutine form of @ref InviteStaffToEntity.
   * @note The outbound email-daemon request is a synchronous call in this
   * variant too.
   * @param[in,out] callback Completion handler, invoked once with (status,
   * body).
   * @param[in] requester Authenticated caller used for the staff-management
   * permission check.
   * @param[in] data Raw JSON body; must remain valid until the task
   * completes.
   */
  virtual drogon::Task<void> InviteStaffToEntityAsync(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data) = 0;  // Pausable version of Invite; the email
                                    // send itself still blocks even here.
};
