/**
 * @file appointmentcontrollerbase.hpp
 * @brief Type-erased interface for appointment-entity CRUD+S operations.
 * @details In plain words, this file lists the jobs every "appointment entity"
 * must support. Those jobs are: Create a new appointment, Read one, Update one,
 * Delete one, and Search through many — together called "CRUD+S". "Type-erased"
 * is a fancy word for hiding the exact type: the rest of the server can call
 * these jobs without knowing which kind of appointment is actually behind them.
 * Each job appears twice: an ordinary version that reports its result through a
 * callback (a "call me when you are done" function), and an "async" version
 * that uses a coroutine (a function that can pause and resume later). This file
 * only declares the jobs; the real work lives in the class that inherits from
 * it.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the doorway the appointments HTTP routes use to reach every
 * appointment controller. Because the functions here are "virtual", one route
 * handler can serve clinics, pharmacies, laboratories and radiology-center
 * appointments alike — the call is forwarded to the right concrete object
 * (chosen with a `std::variant`, a box that can hold one of several types).
 * This file calls into nothing itself, but it is called by the appointments
 * route registry and it is the contract @ref AppointmentController fulfils.
 * Without it, each appointment route would need separate code for every entity
 * type.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Virtual functions (calling the right version through a base pointer):
 * https://en.cppreference.com/w/cpp/language/virtual
 * - C++20 coroutines (functions that pause and resume with co_await/co_return):
 * https://en.cppreference.com/w/cpp/language/coroutines
 * - std::optional (a value that may be absent):
 * https://en.cppreference.com/w/cpp/utility/optional
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this file at most once per source file, so its
              // declarations are never duplicated.

// Gives std::optional: a box that either holds a value or says "there is no
// value".
#include <optional>
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
 * @class AppointmentControllerBase
 * @brief Type-erased interface for appointment-entity CRUD+S operations.
 * @details Declares a synchronous callback form and a coroutine
 * (`drogon::Task<void>`) form of every operation. The appointments route
 * registry reaches concrete controllers through this base via a `std::variant`,
 * so its virtual functions are the dispatch point used by a single route
 * handler for all appointment entity types.
 *
 *          In plain words: "synchronous" means the answer arrives through a
 * callback function, while the coroutine (`drogon::Task<void>`) versions can be
 * paused with co_await so the server can do other work meanwhile. "Virtual
 * functions" mean a call made through this base runs the real object's version.
 * A `std::variant` is a box that can hold one of several controller types.
 *
 *          Invocation contract shared by every method:
 *          - `callback` is `std::function<void(int, const std::string&)>`, it
 * is moved through the call chain and, by contract, invoked once on each
 * completion path, including failures. Its first argument is an @ref
 * api::v2::Http::Status code, its second a response body.
 *          - `requester` is the authenticated caller identity consulted for
 * authorisation.
 *          - `data` is a non-owning view of the raw JSON request body, valid
 * only for the call.
 *          - `_id` is the optional target id; absence is handled by the
 * concrete implementation.
 *
 *          The `*Async` methods must be co_awaited; the HTTP layer wraps them
 * in `drogon::async_run` and turns the callback result into a reply.
 * @note The interface itself is stateless; behaviour lives in @ref
 * AppointmentController.
 * @see AppointmentController, EntityControllerBase, Controller
 */
class AppointmentControllerBase
{
 public:
  /** @brief Default construction; the interface holds no state of its own. */
  explicit AppointmentControllerBase() =
      default;  // "explicit" blocks surprise implicit conversions; "=
                // default" uses the compiler-written constructor.
  /** @brief Move construction is allowed; copies share no operation state. */
  AppointmentControllerBase(AppointmentControllerBase&&) =
      default;  // Moving is fine because there is no state to transfer.
  /** @brief Copy construction is allowed. */
  AppointmentControllerBase(const AppointmentControllerBase&) =
      default;  // Copying is also fine for the same "no state" reason.
  /** @brief Move assignment is allowed. */
  AppointmentControllerBase& operator=(AppointmentControllerBase&&) =
      default;  // Move-assignment is harmless with no data to move.
  /** @brief Copy assignment is allowed. */
  AppointmentControllerBase& operator=(const AppointmentControllerBase&) =
      default;  // Copy-assignment is harmless too.
  /** @brief Virtual destructor so derived controllers can be released through
   * this interface. */
  virtual ~AppointmentControllerBase() =
      default;  // "virtual" ensures deleting through this base pointer still
                // runs the derived class's cleanup.

  // CRUDS — short label meaning Create-Read-Update-Delete-Search, the five
  // jobs grouped below.
  /**
   * @brief Creates an appointment entity from a JSON request body.
   * @param[in,out] callback Completion handler, invoked once with (status,
   * body).
   * @param[in] requester Authenticated caller used for the create-permission
   * check.
   * @param[in] data Raw JSON request body.
   */
  virtual void Create(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view
          data) = 0;  // "= 0" = pure virtual: derived classes MUST implement
                      // this; "virtual" enables base-pointer dispatch.
  /**
   * @brief Coroutine form of @ref Create.
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
      std::string_view
          data) = 0;  // Same job as Create, but returns a pausable task.
  /**
   * @brief Reads one appointment entity identified by an `id`/`schema` JSON
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
      std::string_view  data) = 0;  // Pure virtual read job; the real code
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
      std::string_view  data) = 0;  // Pausable version of Read.
  /**
   * @brief Updates the appointment entity identified by @p _id from a JSON
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
                          _id) = 0;  // "_id" may hold "no value"; the derived
                                     // class decides if that is an error.
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
   * @brief Deletes the appointment entity identified by @p _id.
   * @param[in,out] callback Completion handler, invoked once with (status,
   * body).
   * @param[in] requester Authenticated caller used for the delete-permission
   * check.
   * @param[in] _id Target entity id; a disengaged optional is rejected by the
   * implementation.
   */
  virtual void Delete(
      CALLBACK_&&             callback,
      const Requester&&       requester,
      std::optional<uint64_t> _id) = 0;  // Pure virtual delete job; needs only
                                         // the target id, not a JSON body.
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
   * @brief Runs a paginated search over appointment entities.
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
      std::string_view data) = 0;  // Pure virtual search job; "paginated" means
                                   // results come back in pages via the offset.
};
