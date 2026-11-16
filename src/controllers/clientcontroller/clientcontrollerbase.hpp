/**
 * @file clientcontrollerbase.hpp
 * @brief Abstract contract for client (User/Provider) handlers: CRUDS plus
 * session and account-state operations.
 * @details Declares `ClientControllerBase`, a pure interface for accounts that
 * can log in. It starts with the usual CRUDS set and adds authentication
 * (Login/Logout), account state (Suspend/Activate), password reset and service
 * lookup. Every method is pure virtual (`= 0`), so a concrete class must
 * implement all of them.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Clients (users and providers) need more than plain record storage: they need
 * sessions and a logged-in state. This interface is what the HTTP routing layer
 * programs against for those endpoints. The concrete ClientController (see
 * clientcontroller.hpp) implements it by reusing EntityController for the
 * storage part and delegating sessions to GateKeeper. Without this contract the
 * auth endpoints and the generic entity endpoints would not line up.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Virtual functions / abstract classes:
 * https://en.cppreference.com/w/cpp/language/abstract_class
 * - Callback / std::function:
 * https://en.cppreference.com/w/cpp/utility/functional/function
 * - Coroutines (co_await / drogon::Task):
 * https://en.cppreference.com/w/cpp/language/coroutines
 * - std::optional (a token or id that may be absent):
 * https://en.cppreference.com/w/cpp/utility/optional
 * - Worker threads / concurrency:
 * https://en.cppreference.com/w/cpp/thread/thread
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once

#include <stdint.h>  // For uint64_t client ids.

#include <optional>     // Optional token/id arguments that may be missing.
#include <string>       // The IP address and token strings.
#include <string_view>  // Non-owning view of request bodies.

#include "drogon/utils/coroutine.h"  // Drogon Task type for the Async variants.
// Defines CALLBACK_, the reply function type.
#include "utils/global/callback.hpp"
#include "utils/global/requester.hpp"  // The authenticated principal type.

// The identity/authorization principal supplied by the HTTP layer.
using Requester =
    api::v2::Requester;  // Short name for the identity/authorization principal.

/**
 * @class ClientControllerBase
 * @brief Abstract contract for client (User/Provider) handlers: CRUDS plus
 * session and account-state operations.
 * @details Extends the entity-style CRUDS surface with authentication
 * (Login/Logout), account suspension state (Suspend/Activate), password reset
 * and service lookup. Implementations (ClientController) authorize via
 *          GateKeeper and, for Create/Update, move the expensive password
 * hashing to a dedicated worker pool. `CALLBACK_` is `std::function<void(int,
 * const std::string&)>`; implementations answer exactly once with an HTTP
 * status code and body on every path that completes. Coroutine variants borrow
 * their arguments and must be awaited while those arguments and the controller
 * are alive.
 * @note Not every operation has a coroutine variant: Logout and ResetPassword
 * are synchronous only in this contract. The `ip_address` argument is used by
 * GateKeeper for login/logout bookkeeping.
 * @see ClientController, EntityControllerBase
 */
class ClientControllerBase {
 public:
  ClientControllerBase() = default;  // No state to initialize in an interface.

  ClientControllerBase(const ClientControllerBase&) =
      default;  // Copyable: no state to duplicate.
  ClientControllerBase(ClientControllerBase&&) =
      default;  // Movable for the same reason.
  ClientControllerBase& operator=(const ClientControllerBase&) =
      default;  // Copy assignment is a no-op.
  ClientControllerBase& operator=(ClientControllerBase&&) =
      default;  // Move assignment is a no-op.
  virtual ~ClientControllerBase() =
      default;  // Virtual so deletion through this base is safe.

  // PUBLIC
  /**
   * @brief Creates a client account (hashes the password off the request
   * thread in the implementation).
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body.
   * @param[in] requester Principal used to authorize creation.
   * @param[in] data JSON body describing the new client.
   */
  virtual void Create(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data) = 0;  // Pure virtual: create a client account.
  /**
   * @brief Coroutine form of Create(); must be awaited.
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Principal used to authorize creation.
   * @param[in] data JSON body; must outlive the coroutine.
   */
  virtual drogon::Task<void> CreateAsync(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data) = 0;  // Pure virtual coroutine create.
  /**
   * @brief Reads selected columns of a client identified in @p data.
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body.
   * @param[in] requester Principal used to authorize the read.
   * @param[in] data JSON body containing `id` and `schema`.
   */
  virtual void Read(CALLBACK_&&       callback,
                    const Requester&& requester,
                    std::string_view  data) = 0;  // Pure virtual: read selected
                                                  // client columns.
  /**
   * @brief Coroutine form of Read(); must be awaited.
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Principal used to authorize the read.
   * @param[in] data JSON body containing `id` and `schema`.
   */
  virtual drogon::Task<void> ReadAsync(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data) = 0;  // Pure virtual coroutine read.
  /**
   * @brief Updates a client account (@p _id), re-hashing immutable-checked
   * fields in the implementation.
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body.
   * @param[in] requester Principal used to authorize the update.
   * @param[in] data JSON body holding the fields to change.
   * @param[in] _id Target client id.
   */
  virtual void Update(CALLBACK_&&       callback,
                      const Requester&& requester,
                      std::string_view  data,
                      std::optional<uint64_t>
                          _id) = 0;  // Pure virtual: update a client account.
  /**
   * @brief Coroutine form of Update(); must be awaited.
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Principal used to authorize the update.
   * @param[in] data JSON body holding the fields to change.
   * @param[in] _id Target client id.
   */
  virtual drogon::Task<void> UpdateAsync(
      CALLBACK_&&             callback,
      const Requester&&       requester,
      std::string_view        data,
      std::optional<uint64_t> _id) = 0;  // Pure virtual coroutine update.
  /**
   * @brief Deletes a client and invalidates its session in the
   * implementation.
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body.
   * @param[in] requester Principal used to authorize the deletion.
   * @param[in] client_id Target client id.
   */
  virtual void Delete(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::optional<uint64_t>
          client_id) = 0;  // Pure virtual: delete a client account.
  /**
   * @brief Coroutine form of Delete(); must be awaited.
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Principal used to authorize the deletion.
   * @param[in] client_id Target client id.
   */
  virtual drogon::Task<void> DeleteAsync(
      CALLBACK_&&             callback,
      const Requester&&       requester,
      std::optional<uint64_t> client_id) = 0;  // Pure virtual coroutine delete.
  /**
   * @brief Searches clients according to criteria in @p data.
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body.
   * @param[in] requester Principal supplied by the caller.
   * @param[in] data JSON body containing criteria, limit and offset.
   */
  virtual void Search(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data) = 0;  // Pure virtual: search clients.
  /**
   * @brief Coroutine form of Search(); must be awaited.
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Principal supplied by the caller.
   * @param[in] data JSON body containing criteria, limit and offset.
   */
  virtual drogon::Task<void> SearchAsync(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data) = 0;  // Pure virtual coroutine search.
  /**
   * @brief Authenticates a client and establishes a session through
   * GateKeeper.
   * @param[in,out] callback Invoked once with the login result
   * (token/status/body).
   * @param[in] requester Principal supplied by the caller; unused by the
   * implementation.
   * @param[in] data JSON body containing credentials.
   * @param[in] ip_address Client IP used for session bookkeeping.
   */
  virtual void Login(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data,
      const std::string&
          ip_address) = 0;  // Pure virtual: authenticate and open a session.
  /**
   * @brief Coroutine form of Login(); must be awaited.
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Principal supplied by the caller; unused by the
   * implementation.
   * @param[in] data JSON body containing credentials.
   * @param[in] ip_address Client IP used for session bookkeeping.
   */
  virtual drogon::Task<void> LoginAsync(
      CALLBACK_&&        callback,
      const Requester&&  requester,
      std::string_view   data,
      const std::string& ip_address) = 0;  // Pure virtual coroutine login.
  /**
   * @brief Invalidates the session identified by @p token through GateKeeper.
   * @param[in,out] callback Invoked once with the logout result.
   * @param[in] requester Principal supplied by the caller; unused by the
   * implementation.
   * @param[in] token Session token to invalidate; a missing value lets
   * GateKeeper decide how to respond.
   * @param[in] ip_address Client IP used for session bookkeeping.
   */
  virtual void Logout(
      CALLBACK_&&                       callback,
      const Requester&&                 requester,
      const std::optional<std::string>& token,
      const std::string&
          ip_address) = 0;  // Pure virtual: invalidate a session.
  /**
   * @brief Suspends a client account and removes its active sessions in the
   * implementation.
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body.
   * @param[in] requester Principal whose toggle-active permission is checked.
   * @param[in] client_id Target client id; a missing value is rejected.
   */
  virtual void Suspend(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::optional<uint64_t>
          client_id) = 0;  // Pure virtual: deactivate a client account.
  /**
   * @brief Coroutine form of Suspend(); must be awaited.
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Principal whose toggle-active permission is checked.
   * @param[in] client_id Target client id; a missing value is rejected.
   */
  virtual drogon::Task<void> SuspendAsync(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::optional<uint64_t>
          client_id) = 0;  // Pure virtual coroutine suspend.
  /**
   * @brief Reactivates a previously suspended client account.
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body.
   * @param[in] requester Principal whose toggle-active permission is checked.
   * @param[in] client_id Target client id; a missing value is rejected.
   */
  virtual void Activate(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::optional<uint64_t>
          client_id) = 0;  // Pure virtual: reactivate a client account.
  /**
   * @brief Coroutine form of Activate(); must be awaited.
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Principal whose toggle-active permission is checked.
   * @param[in] client_id Target client id; a missing value is rejected.
   */
  virtual drogon::Task<void> ActivateAsync(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::optional<uint64_t>
          client_id) = 0;  // Pure virtual coroutine activate.
  /**
   * @brief Resets a client's password from @p data.
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body.
   * @param[in] requester Principal supplied by the caller.
   * @param[in] data JSON body containing the credential material.
   */
  virtual void ResetPassword(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data) = 0;  // Pure virtual: password reset hook.
  /**
   * @brief Lists the services associated with a client.
   * @param[in,out] callback Invoked once with 200 and a JSON array (or an
   * error status).
   * @param[in] requester Principal whose services permission is checked.
   * @param[in] client_id Target client id; a missing value is rejected.
   */
  virtual void GetServices(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::optional<uint64_t>
          client_id) = 0;  // Pure virtual: list a client's services.
  /**
   * @brief Coroutine form of GetServices(); must be awaited.
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Principal whose services permission is checked.
   * @param[in] client_id Target client id; a missing value is rejected.
   */
  virtual drogon::Task<void> GetServicesAsync(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::optional<uint64_t>
          client_id) = 0;  // Pure virtual coroutine service lookup.
};
