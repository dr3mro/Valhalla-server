/**
 * @file clientcontroller.hpp
 * @brief Client-specific handler that layers password hashing, sessions and
 * account state over the generic CRUDS flow.
 * @details Declares `ClientController<T>`, which inherits storage from
 * EntityController<T> and the account contract from ClientControllerBase.
 * Simple operations (Read/Search) just forward to the base. Create/Update are
 *          special: they run on a PasswordWorker because hashing is
 * deliberately slow. Login/Logout and account state go through GateKeeper. The
 * definitions and instantiations live in clientcontroller.cpp.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Accounts need protections that ordinary records do not: passwords must be
 * hashed off the request thread, and sessions must be invalidated when an
 * account is deleted or suspended. This class adds exactly those behaviours on
 * top of the generic entity pipeline. Without it, clients would be treated like
 * any other row and password hashing would block the server's IO threads.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Multiple inheritance:
 * https://en.cppreference.com/w/cpp/language/derived_class
 * - `final` overrides (cannot be overridden further):
 * https://en.cppreference.com/w/cpp/language/virtual
 * - Lambdas captured by value for worker jobs:
 * https://en.cppreference.com/w/cpp/language/lambda
 * - Coroutines (co_await / drogon::Task):
 * https://en.cppreference.com/w/cpp/language/coroutines
 * - Concepts / constrained templates (`Client_t`):
 * https://en.cppreference.com/w/cpp/language/constraints
 * - std::shared_ptr (sharing services):
 * https://en.cppreference.com/w/cpp/memory/shared_ptr
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once

#include <fmt/core.h>  // Formatting library used by the implementation.

#include <cstdint>            // For uint64_t client ids.
#include <jsoncons/json.hpp>  // JSON value type.
#include <memory>             // std::shared_ptr for shared services.
#include <optional>           // Optional client ids / tokens.
#include <string>             // IP address and token strings.
#include <string_view>        // Non-owning view of request bodies.

// The interface implemented here.
#include "controllers/clientcontroller/clientcontrollerbase.hpp"
// Base that supplies generic CRUDS.
#include "controllers/entitycontroller/entitycontroller.hpp"
// Generic CRUDS interface type.
#include "controllers/entitycontroller/entitycontrollerbase.hpp"
#include "drogon/utils/coroutine.h"   // Drogon Task type for Async methods.
#include "gatekeeper/gatekeeper.hpp"  // Authorization service.
// Worker pool for slow password hashing.
#include "gatekeeper/passwordcrypt/passwordworker.hpp"
#include "store/store.hpp"            // Service locator for shared services.
#include "utils/global/callback.hpp"  // Defines CALLBACK_.
// Defines the Client_t concept used as template constraint.
#include "utils/global/concepts.hpp"
#include "utils/global/global.hpp"  // Project-wide shared helpers.

/**
 * @class ClientController
 * @brief Client-specific handler that layers password hashing, sessions and
 * account state over the generic CRUDS flow.
 * @details Inherits persistence from EntityController<T> and the full client
 * contract from ClientControllerBase. Read/Search and their coroutine forms
 * simply forward to EntityController; Create/Update dispatch their bodies to a
 * PasswordWorker so password hashing never occupies a Drogon IO thread.
 * Login/Logout are delegated to GateKeeper, and Suspend/Delete additionally
 * invalidate sessions on success.
 * @tparam T Entity type satisfying `Client_t` (derives from `api::v2::Client`),
 * e.g. User or Provider.
 * @note All overrides are `final`. Coroutine variants borrow their arguments
 * and must be awaited while they live.
 * @see ClientControllerBase, EntityController, PasswordWorker
 */
template <Client_t T>
class ClientController : public EntityController<T>, public ClientControllerBase
{
 public:
  ClientController() =
      default;  // Services are initialized by their member initializers.
  ClientController(const ClientController&) =
      default;  // Copies share the two service handles.
  ClientController(ClientController&&) =
      default;  // Move transfers the service handles.
  ClientController& operator=(const ClientController&) =
      default;  // Copy assignment shares the handles.
  ClientController& operator=(ClientController&&) =
      default;  // Move assignment transfers the handles.
  ~ClientController() override =
      default;  // Virtual destructor; no explicit cleanup needed.

  /**
   * @brief Creates a client, hashing the password on the dedicated
   * PasswordWorker.
   * @details Dispatches createNow() to the worker; a full queue yields 503
   * and the body is never run.
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body.
   * @param[in] requester Principal used to authorize creation.
   * @param[in] data JSON body describing the new client (copied into the
   * worker job).
   */
  void Create(CALLBACK_&&       callback,
              const Requester&& requester,
              std::string_view  data)
      final;  // final: no further override; hashing runs on the worker.
  /**
   * @brief Coroutine form of Create(); currently delegates straight to
   * EntityController::CreateAsync.
   * @note Unlike the synchronous Create(), this path does not go through the
   * PasswordWorker.
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Principal used to authorize creation.
   * @param[in] data JSON body; must outlive the coroutine.
   */
  drogon::Task<void> CreateAsync(CALLBACK_&&       callback,
                                 const Requester&& requester,
                                 std::string_view  data)
      final;  // final coroutine create; delegates to EntityController.
  /**
   * @brief Reads selected columns of a client; delegates to
   * EntityController::Read().
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body.
   * @param[in] requester Principal used to authorize the read.
   * @param[in] data JSON body containing `id` and `schema`.
   */
  void Read(CALLBACK_&&       callback,
            const Requester&& requester,
            std::string_view  data)
      final;  // final: forwards straight to EntityController::Read.
  /**
   * @brief Coroutine form of Read(); delegates to
   * EntityController::ReadAsync().
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Principal used to authorize the read.
   * @param[in] data JSON body containing `id` and `schema`; must outlive the
   * coroutine.
   */
  drogon::Task<void> ReadAsync(CALLBACK_&&       callback,
                               const Requester&& requester,
                               std::string_view  data)
      final;  // final coroutine read; forwards to the base.
  /**
   * @brief Updates a client, hashing the password on the dedicated
   * PasswordWorker.
   * @details The `username` and `password` fields are immutable (validated in
   * updateNow()).
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body.
   * @param[in] requester Principal used to authorize the update.
   * @param[in] data JSON body holding the fields to change.
   * @param[in] _id Target client id.
   */
  void Update(CALLBACK_&&             callback,
              const Requester&&       requester,
              std::string_view        data,
              std::optional<uint64_t> _id)
      final;  // final: hashing on worker; immutable fields checked.
  /**
   * @brief Coroutine form of Update(); delegates straight to
   * EntityController::UpdateAsync().
   * @note Unlike the synchronous Update(), this path does not go through the
   * PasswordWorker.
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Principal used to authorize the update.
   * @param[in] data JSON body holding the fields to change; must outlive the
   * coroutine.
   * @param[in] _id Target client id.
   */
  drogon::Task<void> UpdateAsync(CALLBACK_&&             callback,
                                 const Requester&&       requester,
                                 std::string_view        data,
                                 std::optional<uint64_t> _id)
      final;  // final coroutine update; delegates to base.
  /**
   * @brief Deletes a client and removes its sessions when the database
   * reports success.
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body.
   * @param[in] requester Principal used to authorize the deletion.
   * @param[in] client_id Target client id.
   */
  void Delete(CALLBACK_&&             callback,
              const Requester&&       requester,
              std::optional<uint64_t> client_id)
      final;  // final: wraps callback to drop sessions on success.
  /**
   * @brief Coroutine form of Delete(); wraps the callback as in Delete()
   * before delegating.
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Principal used to authorize the deletion.
   * @param[in] client_id Target client id.
   */
  drogon::Task<void> DeleteAsync(CALLBACK_&&             callback,
                                 const Requester&&       requester,
                                 std::optional<uint64_t> client_id)
      final;  // final coroutine delete with session cleanup.
  /**
   * @brief Searches clients; delegates to EntityController::Search().
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body.
   * @param[in] requester Principal supplied by the caller.
   * @param[in] data JSON body containing criteria, limit and offset.
   */
  void Search(CALLBACK_&&       callback,
              const Requester&& requester,
              std::string_view  data)
      final;  // final: forwards to EntityController::Search.
  /**
   * @brief Coroutine form of Search(); delegates to
   * EntityController::SearchAsync().
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Principal supplied by the caller.
   * @param[in] data JSON body containing criteria, limit and offset; must
   * outlive the coroutine.
   */
  drogon::Task<void> SearchAsync(CALLBACK_&&       callback,
                                 const Requester&& requester,
                                 std::string_view  data)
      final;  // final coroutine search; forwards to base.
  /**
   * @brief Authenticates a client via GateKeeper; the requester argument is
   * ignored.
   * @param[in,out] callback Invoked once with the login result.
   * @param[in] requester Unused principal.
   * @param[in] data JSON body containing credentials.
   * @param[in] ip_address Client IP used for session bookkeeping.
   */
  void Login(CALLBACK_&&        callback,
             const Requester&&  requester,
             std::string_view   data,
             const std::string& ip_address)
      final;  // final: authentication delegated to GateKeeper.
  /**
   * @brief Coroutine form of Login(); awaits GateKeeper::loginAsync().
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Unused principal.
   * @param[in] data JSON body containing credentials; must outlive the
   * coroutine.
   * @param[in] ip_address Client IP used for session bookkeeping.
   */
  drogon::Task<void> LoginAsync(CALLBACK_&&        callback,
                                const Requester&&  requester,
                                std::string_view   data,
                                const std::string& ip_address)
      final;  // final coroutine login via GateKeeper.
  /**
   * @brief Invalidates a client session via GateKeeper; the requester
   * argument is ignored.
   * @param[in,out] callback Invoked once with the logout result.
   * @param[in] requester Unused principal.
   * @param[in] token Session token to invalidate.
   * @param[in] ip_address Client IP used for session bookkeeping.
   */
  void Logout(CALLBACK_&&                       callback,
              const Requester&&                 requester,
              const std::optional<std::string>& token,
              const std::string&                ip_address)
      final;  // final: session invalidation delegated to GateKeeper.
  /**
   * @brief Suspends a client (if authorized) and removes its sessions after
   * the DB update.
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body.
   * @param[in] requester Principal whose toggle-active permission is checked.
   * @param[in] client_id Target client id; a missing value yields 406.
   */
  void Suspend(CALLBACK_&&             callback,
               const Requester&&       requester,
               std::optional<uint64_t> client_id)
      final;  // final: deactivate and remove sessions.
  /**
   * @brief Coroutine form of Suspend(); must be awaited.
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Principal whose toggle-active permission is checked.
   * @param[in] client_id Target client id; a missing value yields 406.
   */
  drogon::Task<void> SuspendAsync(
      CALLBACK_&&             callback,
      const Requester&&       requester,
      std::optional<uint64_t> client_id) final;  // final coroutine suspend.
  /**
   * @brief Reactivates a suspended client (if authorized); no session side
   * effect.
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body.
   * @param[in] requester Principal whose toggle-active permission is checked.
   * @param[in] client_id Target client id; a missing value yields 406.
   */
  void Activate(CALLBACK_&&             callback,
                const Requester&&       requester,
                std::optional<uint64_t> client_id)
      final;  // final: reactivate; sessions untouched.
  /**
   * @brief Coroutine form of Activate(); must be awaited.
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Principal whose toggle-active permission is checked.
   * @param[in] client_id Target client id; a missing value yields 406.
   */
  drogon::Task<void> ActivateAsync(
      CALLBACK_&&             callback,
      const Requester&&       requester,
      std::optional<uint64_t> client_id) final;  // final coroutine activate.
  /**
   * @brief Password reset hook.
   * @warning The current implementation is a no-op: it discards @p callback
   * and @p data and never answers. Callers waiting on the callback will not
   * be completed.
   * @param[in,out] callback Currently ignored.
   * @param[in] requester Principal supplied by the caller; unused.
   * @param[in] data Credential material; currently ignored.
   */
  void ResetPassword(CALLBACK_&&       callback,
                     const Requester&& requester,
                     std::string_view  data)
      final;  // final: currently a no-op (see @warning above).
  /**
   * @brief Lists services available to a client (if authorized).
   * @param[in,out] callback Invoked once with 200 and a JSON array, or an
   * error status.
   * @param[in] requester Principal whose services permission is checked.
   * @param[in] client_id Target client id; a missing value yields 400.
   */
  void GetServices(CALLBACK_&&             callback,
                   const Requester&&       requester,
                   std::optional<uint64_t> client_id)
      final;  // final: authorized service lookup.
  /**
   * @brief Coroutine form of GetServices(); must be awaited.
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Principal whose services permission is checked.
   * @param[in] client_id Target client id; a missing value yields 400.
   */
  drogon::Task<void> GetServicesAsync(CALLBACK_&&             callback,
                                      const Requester&&       requester,
                                      std::optional<uint64_t> client_id)
      final;  // final coroutine service lookup.

 private:
  // The real work of Create and Update. Both hash the password, so the public
  // methods run them on the password worker instead of on a request thread.
  /**
   * @brief Body executed on the password worker by Create(): parses, checks
   * existence/authorization, inserts.
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body.
   * @param[in] requester Principal used to authorize creation (copied into
   * the worker job).
   * @param[in] data JSON body; copied into the worker job so it outlives the
   * request thread.
   */
  void createNow(CALLBACK_&&      callback,
                 const Requester& requester,
                 std::string_view data);
  /**
   * @brief Body executed on the password worker by Update(): validates
   * immutable fields, authorizes, updates.
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body.
   * @param[in] requester Principal used to authorize the update.
   * @param[in] data JSON body holding the fields to change.
   * @param[in] _id Target client id.
   */
  void updateNow(CALLBACK_&&             callback,
                 const Requester&        requester,
                 std::string_view        data,
                 std::optional<uint64_t> _id);

  /**
   * Shared authorization service used by all operations.
   */
  std::shared_ptr<GateKeeper> gateKeeper =
      Store::getObject<GateKeeper>();  // Shared authorization service.
  /**
   * Dedicated pool for the deliberately slow password hashing/verification
   * work.
   */
  std::shared_ptr<PasswordWorker> passwordWorker =
      Store::getObject<PasswordWorker>();  // Bounded pool for slow hashing
                                           // jobs.
};
