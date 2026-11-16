/**
 * @file servicecontrollerbase.hpp
 * @brief Type-erased interface for service-entity (clinic, pharmacy,
 * laboratory, radiology center) CRUD+S operations.
 * @details In everyday language, this file lists the jobs every "service
 * entity" (a clinic, a pharmacy, a laboratory or a radiology center) must be
 * able to do. Those jobs are: Create a new one, Read one, Update one, Delete
 * one, and Search through many — together called "CRUD+S". "Type-erased" is a
 *          fancy word for hiding the exact type: the rest of the server can
 * call these jobs without knowing which kind of entity is actually behind them.
 * Each job appears twice: an ordinary version that reports its result through a
 * callback (a "call me when you are done" function), and an "async" version
 * that uses a coroutine (a function that can pause and resume later). This file
 * only declares the jobs; the real work is written in the class that inherits
 * from this one.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the doorway the HTTP routing layer uses to reach every service
 * controller. Because all the job functions here are "virtual", one single
 * route handler can serve clinics, pharmacies, laboratories and radiology
 * centers without knowing which one it is talking to — the call is
 * automatically forwarded to the right concrete controller. This file calls
 * into nothing itself, but it is called by the services route registry and it
 * is the contract that @ref ServiceController fulfils. Without it, every route
 * would need separate code for each entity type and adding a new service type
 * would mean rewriting the routes.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Virtual functions (calling the right version through a base pointer):
 * https://en.cppreference.com/w/cpp/language/virtual
 * - C++20 coroutines (functions that pause and resume with co_await/co_return):
 * https://en.cppreference.com/w/cpp/language/coroutines
 * - C++20 concepts / requires (compile-time checks on template arguments):
 * https://en.cppreference.com/w/cpp/language/constraints
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

// Pulls in the appointment interface that this service interface mirrors (same
// callback/requester conventions).
#include "controllers/appointmentcontroller/appointmentcontrollerbase.hpp"
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
 * @class ServiceControllerBase
 * @brief Type-erased interface for service-entity (clinic, pharmacy,
 * laboratory, radiology center) CRUD+S operations.
 * @details Declares both a synchronous, callback-driven form of every operation
 * and a coroutine
 *          (`drogon::Task<void>`) form. The HTTP layer reaches concrete
 * controllers through this base via a `std::variant` in the services route
 * registry, so virtual dispatch here is what makes one route handler serve all
 * service entity types.
 *
 *          In plain words: "synchronous" means the caller gets the answer
 * through a callback function; "coroutine" (`drogon::Task<void>`) means the
 * caller can pause the operation with co_await and let the server do other work
 * meanwhile. "Virtual dispatch" means that when the route code calls `Create`
 * through this base type, C++ automatically runs the version belonging to the
 * real object (for example the clinic controller). A `std::variant` is a
 * variable that can hold one of several different types — here it holds
 * whichever concrete service controller was selected.
 *
 *          Invocation contract shared by every method:
 *          - `callback` is `std::function<void(int, const std::string&)>`,
 * moved by the caller and forwarded down the call chain. By contract it is
 * invoked once on each completion path, including failures; the first argument
 * is an @ref api::v2::Http::Status code, the second a response body (serialized
 * row(s) on success, diagnostic text on failure).
 *          - `requester` is the authenticated caller identity, used by
 * implementations for the permission decision before any database work is done.
 *          - `data` is a non-owning view of the raw JSON request body; it must
 * outlive the call.
 *          - `_id`, where present, is the optional target entity id; a missing
 * value is rejected by the concrete implementation rather than by this
 * interface.
 *
 *          The `*Async` methods perform the same operation but must be
 * co_awaited; the route layer wraps them in `drogon::async_run` and converts
 * the callback result into an HTTP reply.
 * @note Implementations here are supplied by @ref ServiceController; the
 * interface itself adds no state.
 * @see ServiceController, EntityControllerBase, Controller
 */
class ServiceControllerBase {
 public:
  /** @brief Default construction; the interface holds no state of its own. */
  explicit ServiceControllerBase() =
      default;  // "explicit" prevents surprise implicit conversions; "=
                // default" lets the compiler write this simple constructor
                // for us.

  /** @brief Copy construction is allowed; copies share no operation state. */
  ServiceControllerBase(const ServiceControllerBase&) =
      default;  // Making a copy is fine because this base stores no data of
                // its own.
  /** @brief Move construction is disabled; the interface is used through
   * pointers/references. */
  ServiceControllerBase(ServiceControllerBase&&) =
      delete;  // "= delete" makes any attempt to move this class a
               // compile-time error.
  /** @brief Copy assignment is allowed. */
  ServiceControllerBase& operator=(const ServiceControllerBase&) =
      default;  // Copy-assignment is also harmless because there is no state
                // to copy.
  /** @brief Move assignment is disabled, mirroring the deleted move
   * constructor. */
  ServiceControllerBase& operator=(ServiceControllerBase&&) =
      delete;  // Move-assignment is blocked for the same reason as the move
               // constructor.
  /**
   * @brief Virtual destructor so derived controllers can be destroyed through
   * this interface.
   * @note Route registries actually store `shared_ptr` to the concrete
   * controller type.
   */
  virtual ~ServiceControllerBase() =
      default;  // "virtual" guarantees that deleting through this base
                // pointer still runs the derived class's cleanup code.

  // CRUDS — short label meaning Create-Read-Update-Delete-Search, the five
  // jobs grouped below.
  /**
   * @brief Creates a service entity from a JSON request body.
   * @param[in,out] callback Completion handler, invoked once with (status,
   * body).
   * @param[in] requester Authenticated caller used for the create-permission
   * check.
   * @param[in] data Raw JSON request body.
   * @see EntityController::Create for the shipped validation/permission/error
   * mapping.
   */
  virtual void Create(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data) = 0;  // "= 0" makes this pure virtual: a derived
                                    // class MUST implement it, and "virtual"
                                    // lets a base pointer call it.
  /**
   * @brief Coroutine form of @ref Create.
   * @details Must be co_awaited; the callback fires when the awaited work
   * completes.
   * @param[in,out] callback Completion handler, invoked once with (status,
   * body).
   * @param[in] requester Authenticated caller used for the create-permission
   * check.
   * @param[in] data Raw JSON request body; must remain valid until the task
   * completes.
   */
  virtual drogon::Task<void> CreateAsync(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view data) = 0;  // Same job as Create, but returns a pausable
                                   // task instead of finishing immediately.
  /**
   * @brief Reads one service entity identified by an `id`/`schema` JSON
   * request.
   * @param[in,out] callback Completion handler, invoked once with (status,
   * body).
   * @param[in] requester Authenticated caller used for the read-permission
   * check.
   * @param[in] data Raw JSON request body containing `id` and requested
   * `schema`.
   */
  virtual void Read(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data) = 0;  // Pure virtual read job: the real code
                                    // lives in the derived controller.
  /**
   * @brief Coroutine form of @ref Read.
   * @param[in,out] callback Completion handler, invoked once with (status,
   * body).
   * @param[in] requester Authenticated caller used for the read-permission
   * check.
   * @param[in] data Raw JSON request body; must remain valid until the task
   * completes.
   */
  virtual drogon::Task<void> ReadAsync(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view
          data) = 0;  // Read as a pausable task, to be used with co_await.
  /**
   * @brief Updates the service entity identified by @p _id from a JSON
   * request body.
   * @param[in,out] callback Completion handler, invoked once with (status,
   * body).
   * @param[in] requester Authenticated caller used for the update-permission
   * check.
   * @param[in] data Raw JSON request body with the fields to change.
   * @param[in] _id Target entity id; a disengaged optional is rejected by the
   * implementation.
   */
  virtual void Update(CALLBACK_&&       callback,
                      const Requester&& requester,
                      std::string_view  data,
                      std::optional<uint64_t>
                          _id) = 0;  // "_id" may be "no value"; the derived
                                     // class decides whether that is an error.
  /**
   * @brief Coroutine form of @ref Update.
   * @param[in,out] callback Completion handler, invoked once with (status,
   * body).
   * @param[in] requester Authenticated caller used for the update-permission
   * check.
   * @param[in] data Raw JSON request body; must remain valid until the task
   * completes.
   * @param[in] _id Target entity id; a disengaged optional is rejected by the
   * implementation.
   */
  virtual drogon::Task<void> UpdateAsync(
      CALLBACK_&&             callback,
      const Requester&&       requester,
      std::string_view        data,
      std::optional<uint64_t> _id) = 0;  // Pausable form of Update.
  /**
   * @brief Deletes the service entity identified by @p _id.
   * @param[in,out] callback Completion handler, invoked once with (status,
   * body).
   * @param[in] requester Authenticated caller used for the delete-permission
   * check.
   * @param[in] _id Target entity id; a disengaged optional is rejected by the
   * implementation.
   */
  virtual void Delete(CALLBACK_&&       callback,
                      const Requester&& requester,
                      std::optional<uint64_t>
                          _id) = 0;  // Pure virtual delete job; note it needs
                                     // no JSON body, only the target id.
  /**
   * @brief Coroutine form of @ref Delete.
   * @param[in,out] callback Completion handler, invoked once with (status,
   * body).
   * @param[in] requester Authenticated caller used for the delete-permission
   * check.
   * @param[in] _id Target entity id; a disengaged optional is rejected by the
   * implementation.
   */
  virtual drogon::Task<void> DeleteAsync(
      CALLBACK_&&             callback,
      const Requester&&       requester,
      std::optional<uint64_t> _id) = 0;  // Pausable form of Delete.
  /**
   * @brief Runs a paginated search over service entities.
   * @details Implementations return a JSON object with `results`, `more` and
   * `offset`; the requester is accepted for interface symmetry but need not
   * be consulted.
   * @param[in,out] callback Completion handler, invoked once with (status,
   * body).
   * @param[in] requester Authenticated caller (unused by the shipped search
   * implementation).
   * @param[in] data Raw JSON search criteria.
   */
  virtual void Search(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view
          data) = 0;  // Pure virtual search job; "paginated" means results
                      // come back in pages using the offset.
};
