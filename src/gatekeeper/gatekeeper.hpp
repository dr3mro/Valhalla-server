/**
 * @file gatekeeper.hpp
 * @brief Facade over authentication, authorization and per-IP abuse detection.
 * @details GateKeeper is the single "front desk" the rest of the server talks
 * to for security. It exposes login/logout, session-or-JWT authentication
 * checks, permission questions (create/read/update/delete and a few special
 * cases, each in a normal and a coroutine version), and a request classifier
 * used for abuse protection. It owns no algorithms of its own: every call is
 * forwarded to a collaborator (SessionManager, TokenManager, DOSDetector,
 * PermissionManager, PasswordWorker) fetched from the Store singleton. Because
 * those collaborators are shared, separate GateKeeper objects all see the same
 *          sessions, tokens and counters.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the public face of the gatekeeper/security subsystem. Drogon
 * controllers and filters create or receive a GateKeeper and call it to log a
 * user in, decide whether a request is authenticated, check permissions before
 * touching data, and ask whether an IP is abusing the service. GateKeeper
 * itself calls down into the five manager classes listed above. Without this
 * facade every controller would have to know all five managers and wire them
 * together correctly; with it, security is reached through one simple, stable
 * interface. The header declares the interface (what can be asked);
 * gatekeeper.cpp holds the implementation.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Facade pattern (one simple interface over several complex subsystems):
 * https://en.wikipedia.org/wiki/Facade_pattern
 * - JWT (a signed token carrying identity claims): https://jwt.io/introduction
 * - Singleton / service locator (one shared instance looked up by type):
 * https://en.wikipedia.org/wiki/Singleton_pattern
 * - std::shared_ptr (shared ownership of an object):
 * https://en.cppreference.com/w/cpp/memory/shared_ptr
 * - std::optional ("a value that may be missing"):
 * https://en.cppreference.com/w/cpp/utility/optional
 * - Template function (one definition that works for many types):
 * https://en.cppreference.com/w/cpp/language/function_template
 * - Coroutine / co_await (pausing work without blocking a thread):
 * https://en.cppreference.com/w/cpp/language/coroutines
 * - Mutex/lock: https://en.cppreference.com/w/cpp/thread/mutex
 * - HTTP status codes: https://developer.mozilla.org/en-US/docs/Web/HTTP/Status
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header only once per translation unit.
// Drogon template base; pulled in by the framework's coroutine plumbing.
#include <drogon/DrTemplateBase.h>

// For uint64_t, the fixed-width id type used by permission helpers.
#include <cstdint>
// Core jsoncons JSON type used by the parser helpers.
#include <jsoncons/basic_json.hpp>
// Convenience JSON header (jsoncons::json) used in signatures.
#include <jsoncons/json.hpp>
#include <memory>    // For std::shared_ptr, the shared handles to the managers.
#include <optional>  // For std::optional used in parameters and return types.
#include <string>    // For std::string parameters and messages.
// For std::string_view, a non-owning view of request data.
#include <string_view>

#include "drogon/utils/coroutine.h"  // drogon::Task, the coroutine return type.
// DOSDetector::Status and Request used by isDosAttack.
#include "gatekeeper/dosdetector/dosdetector.hpp"
// PasswordWorker held as a collaborator.
#include "gatekeeper/passwordcrypt/passwordworker.hpp"
// PermissionManager held as a collaborator.
#include "gatekeeper/permissionmanager/permissionmanager.hpp"
// SessionManager held as a collaborator.
#include "gatekeeper/sessionmanager/sessionmanager.hpp"
// TokenManager held as a collaborator.
#include "gatekeeper/tokenmanager/tokenmanager.hpp"
// Types::Credentials and Types::ClientLoginData.
#include "gatekeeper/types.hpp"
// CALLBACK_, the completion-callback type.
#include "utils/global/callback.hpp"
// Client_t concept constraining client-only helpers.
#include "utils/global/concepts.hpp"
// Http::Error / Http::Status used in signatures.
#include "utils/global/http.hpp"
// Requester, the caller identity passed to permission checks.
#include "utils/global/requester.hpp"

namespace api::v2  // All public API v2 code lives in this namespace.
{
  /**
   * @class GateKeeper
   * @brief Facade over authentication, authorization and per-IP abuse
   * detection.
   * @details GateKeeper is the single entry point used by controllers and
   * filters for: login/logout, validating a client's session or JWT,
   * answering permission queries (synchronous and coroutine variants), and
   * classifying requests for DoS/rate-limit purposes. It owns no algorithmic
   * state of its own: every operation is delegated to a collaborator obtained
   * from the Store singleton (SessionManager, TokenManager, DOSDetector,
   * PermissionManager, PasswordWorker). Consequently, distinct GateKeeper
   * instances share the same underlying managers, caches and state. Any
   * thread-safety guarantees are those of the collaborators, not of
   * GateKeeper itself.
   * @note This class is polymorphic (the authentication validators are
   * virtual) so it can be specialized in tests or alternative deployments.
   * @see SessionManager, TokenManager, PermissionManager, DOSDetector,
   * PasswordWorker
   * @details For a beginner: a "facade" is like a receptionist. A controller
   * does not need to know which manager to call or in what order; it just
   * asks GateKeeper. Because the manager pointers are shared_ptrs obtained
   * from a process-wide Store, building two GateKeepers does not create two
   * sets of sessions - both talk to the same managers. The methods ending in
   * "Async" are coroutine versions: they return a drogon::Task that the
   * caller must drive, allowing slow database work to pause instead of
   *          blocking a server IO thread.
   */
  class GateKeeper
  {
   public:
    /**
     * @brief Constructs the facade, resolving collaborators from the Store
     * singleton.
     * @details Each collaborator is held via shared_ptr, so this object
     * shares ownership of the process-wide instances rather than creating
     * private ones.
     * @details In other words, construction just grabs five
     * already-existing shared objects from Store; it does not open a
     * database or start any threads itself.
     */
    GateKeeper();
    GateKeeper(const GateKeeper& other) =
        default;  // Copying just copies the shared handles, so copies share
                  // the same managers.
    GateKeeper& operator=(const GateKeeper& other) =
        default;  // Copy assignment likewise only copies handles.
    GateKeeper(GateKeeper&& other) noexcept =
        default;  // Moving transfers the handles; noexcept promises it
                  // never throws.
    GateKeeper& operator=(GateKeeper&& other) noexcept =
        default;  // Move assignment transfers handles without throwing.
    /**
     * @brief Virtual destructor; permits destruction through a base
     * pointer.
     */
    virtual ~GateKeeper() =
        default;  // Virtual so derived test classes are destroyed correctly
                  // through a GateKeeper*.

    /**
     * @brief Authenticates a client and issues a token, reporting the
     * outcome through a callback.
     * @details Parses the JSON credentials on the calling thread, reads the
     * stored password hash with SessionManager, then dispatches the slow
     * password verification and the state-changing work (session
     * completion, token generation) to the password worker so no Drogon IO
     * thread is blocked. Early failures (parse or hash lookup) are reported
     * directly on the calling thread. Errors are caught and translated into
     * an HTTP response via the CRITICALMESSAGERESPONSE macro.
     * @param[in,out] callback Invoked exactly once with the HTTP status and
     * either an error message or a JSON string containing
     * token/username/client_id/group/ipAddress.
     * @param[in] data JSON request body expected to contain "username" and
     * "password".
     * @param[in] ip_address Client IP, bound into the resulting token.
     * @param[in] group Client group, used to select the session table.
     * @details Step by step: validate the JSON, look up the hash, then hand
     * the slow bcrypt check plus the database write and token creation to a
     * worker thread. The callback is the "reply envelope" - it is called
     * once with a status code and a body when the answer is ready, which
     * may be later on another thread.
     */
    void login(CALLBACK_&&        callback,
               std::string_view   data,
               const std::string& ip_address,
               std::string_view   group);
    /**
     * @brief Coroutine form of login() for use on Drogon executor threads.
     * @details Mirrors login() but awaits the stored-hash lookup
     * cooperatively (SessionManager::fetchPasswordHashCoro) instead of
     * blocking on it. The slow verification and all writes still run on the
     * password worker. Early errors and the final worker result are
     * delivered through @p callback.
     * @param[in,out] callback Completion callback, invoked with status and
     * body/message.
     * @param[in] data JSON request body containing the credentials.
     * @param[in] ip_address Client IP bound into the token.
     * @param[in] group Client group selecting the session table.
     * @note The coroutine frame must remain alive until the awaited
     * database read completes; callers must co_await (or otherwise drive)
     * the returned task.
     */
    drogon::Task<void> loginAsync(CALLBACK_&&        callback,
                                  std::string_view   data,
                                  const std::string& ip_address,
                                  std::string_view   group);
    /**
     * @brief Ends a client session, recording the logout time.
     * @details Delegates to SessionManager::logout. The supplied token is
     * merely carried in ClientLoginData for downstream handling; this
     * method does not itself validate it.
     * @param[in,out] callback Completion callback (status and
     * message/body).
     * @param[in] token Optional token associated with the session being
     * closed.
     * @param[in] ip_address Client IP recorded in the logout data.
     * @param[in] group Client group selecting the session table.
     */
    void logout(CALLBACK_&&                       callback,
                const std::optional<std::string>& token,
                const std::string&                ip_address,
                const std::string&                group);

    /**
     * @brief Checks whether the supplied client is authenticated.
     * @details Session cache first: if SessionManager reports a valid
     * session, returns true immediately. Otherwise falls back to JWT
     * validation via TokenManager; on a valid token, the session is
     * (re)established with storeSession() and its result becomes the final
     * answer. This cache-then-token ordering avoids a signature check on
     * every request while still honouring token revocation data.
     * @param[in,out] clientLoginData Identity/state accumulator; may be
     * updated with session fields.
     * @param[out] message Receives a human-readable reason on failure.
     * @return True if an existing or freshly established session authorizes
     * the client.
     * @details "Cache first" is an optimization: checking a local session
     * record is cheaper than verifying a signed token. Only when there is
     * no live session does it pay for the token check, and a token that is
     * about to expire is checked against the latest logout/active data from
     * the database before being trusted.
     */
    virtual bool isAuthenticationValid(
        std::optional<Types::ClientLoginData>& clientLoginData,
        std::string&                           message);
    /**
     * @brief Coroutine form of isAuthenticationValid().
     * @details Same cache-then-token policy, but awaits
     * TokenManager::isTokenValidCoro instead of blocking on the token
     * check.
     * @param[in,out] clientLoginData Identity/state accumulator.
     * @param[out] message Failure reason.
     * @return True when the client is authenticated.
     * @note Callers must drive the returned task; the frame must outlive
     * the awaited read.
     */
    virtual drogon::Task<bool> isAuthenticationValidAsync(
        std::optional<Types::ClientLoginData>& clientLoginData,
        std::string&                           message);
    /**
     * @brief Removes a client's cached session.
     * @param[in] client_id Optional id of the client whose session should
     * be dropped.
     * @param[in] group Client group selecting the session table.
     */
    void removeSession(std::optional<uint64_t> client_id,
                       const std::string&      group);

    /**
     * @brief Checks whether @p requester may create a @p T resource.
     * @details Thin forwarder to PermissionManager::canCreate<T>; the @p
     * data_j payload is inspected for ownership/relationship checks
     * required by the create rule.
     * @tparam T Entity type being created.
     * @param[in] requester Authenticated caller (id and group).
     * @param[in] data_j Optional JSON payload of the prospective resource.
     * @param[out] error Receives denial/error details from the permission
     * manager.
     * @return True if the operation is permitted.
     */
    template <typename T>
    bool canCreate(const Requester&                     requester,
                   const std::optional<jsoncons::json>& data_j,
                   Http::Error&                         error);

    /**
     * @brief Coroutine form of canCreate().
     * @tparam T Entity type being created.
     * @param[in] requester Authenticated caller.
     * @param[in] data_j Optional JSON payload of the prospective resource.
     * @param[out] error Receives denial/error details.
     * @return True if the operation is permitted.
     * @note Callers must drive the returned task; the frame must outlive
     * the awaited checks.
     */
    template <typename T>
    drogon::Task<bool> canCreateAsync(
        const Requester&                     requester,
        const std::optional<jsoncons::json>& data_j,
        Http::Error&                         error);

    /**
     * @brief Checks read permission on the entity @p entity_id.
     * @tparam T Entity type being read.
     * @param[in] requester Authenticated caller.
     * @param[in] entity_id Id of the entity being accessed.
     * @param[out] error Receives denial/error details.
     * @return True if the operation is permitted.
     */
    template <typename T>
    bool canRead(const Requester& requester,
                 uint64_t         entity_id,
                 Http::Error&     error);

    /**
     * @brief Coroutine form of canRead().
     * @tparam T Entity type being read.
     * @param[in] requester Authenticated caller.
     * @param[in] entity_id Id of the entity being accessed.
     * @param[out] error Receives denial/error details.
     * @return True if the operation is permitted.
     * @note Callers must drive the returned task.
     */
    template <typename T>
    drogon::Task<bool> canReadAsync(const Requester& requester,
                                    uint64_t         entity_id,
                                    Http::Error&     error);

    /**
     * @brief Checks update permission on the entity @p entity_id.
     * @tparam T Entity type being updated.
     * @param[in] requester Authenticated caller.
     * @param[in] entity_id Id of the entity being accessed.
     * @param[out] error Receives denial/error details.
     * @return True if the operation is permitted.
     */
    template <typename T>
    bool canUpdate(const Requester& requester,
                   uint64_t         entity_id,
                   Http::Error&     error);

    /**
     * @brief Coroutine form of canUpdate().
     * @tparam T Entity type being updated.
     * @param[in] requester Authenticated caller.
     * @param[in] entity_id Id of the entity being accessed.
     * @param[out] error Receives denial/error details.
     * @return True if the operation is permitted.
     * @note Callers must drive the returned task.
     */
    template <typename T>
    drogon::Task<bool> canUpdateAsync(const Requester& requester,
                                      uint64_t         entity_id,
                                      Http::Error&     error);

    /**
     * @brief Checks delete permission on the entity @p entity_id.
     * @tparam T Entity type being deleted.
     * @param[in] requester Authenticated caller.
     * @param[in] entity_id Id of the entity being accessed.
     * @param[out] error Receives denial/error details.
     * @return True if the operation is permitted.
     */
    template <typename T>
    bool canDelete(const Requester& requester,
                   uint64_t         entity_id,
                   Http::Error&     error);

    /**
     * @brief Coroutine form of canDelete().
     * @tparam T Entity type being deleted.
     * @param[in] requester Authenticated caller.
     * @param[in] entity_id Id of the entity being accessed.
     * @param[out] error Receives denial/error details.
     * @return True if the operation is permitted.
     * @note Callers must drive the returned task.
     */
    template <typename T>
    drogon::Task<bool> canDeleteAsync(const Requester& requester,
                                      uint64_t         entity_id,
                                      Http::Error&     error);

    /**
     * @brief Checks whether @p requester may manage staff of the service @p
     * _id.
     * @tparam T Service/entity type whose staff are being managed.
     * @param[in] requester Authenticated caller.
     * @param[in] _id Id of the service whose staff are targeted.
     * @param[out] error Receives denial/error details.
     * @return True if the operation is permitted.
     */
    template <typename T>
    bool canManageStaff(const Requester& requester,
                        uint64_t         _id,
                        Http::Error&     error);

    /**
     * @brief Coroutine form of canManageStaff().
     * @tparam T Service/entity type whose staff are being managed.
     * @param[in] requester Authenticated caller.
     * @param[in] _id Id of the service whose staff are targeted.
     * @param[out] error Receives denial/error details.
     * @return True if the operation is permitted.
     * @note Callers must drive the returned task.
     */
    template <typename T>
    drogon::Task<bool> canManageStaffAsync(const Requester& requester,
                                           uint64_t         _id,
                                           Http::Error&     error);

    /**
     * @brief Checks whether @p requester may toggle the active flag of
     * client @p _id.
     * @tparam T Client type (User or Provider), constrained by Client_t.
     * @param[in] requester Authenticated caller.
     * @param[in] _id Id of the client whose status is targeted.
     * @param[out] error Receives denial/error details.
     * @return True if the operation is permitted.
     */
    template <Client_t T>
    bool canToggleActive(const Requester& requester,
                         uint64_t         _id,
                         Http::Error&     error);

    /**
     * @brief Coroutine form of canToggleActive().
     * @tparam T Client type, constrained by Client_t.
     * @param[in] requester Authenticated caller.
     * @param[in] _id Id of the client whose status is targeted.
     * @param[out] error Receives denial/error details.
     * @return True if the operation is permitted.
     * @note Callers must drive the returned task.
     */
    template <Client_t T>
    drogon::Task<bool> canToggleActiveAsync(const Requester& requester,
                                            uint64_t         _id,
                                            Http::Error&     error);

    /**
     * @brief Checks whether @p requester may list the services of client @p
     * _id.
     * @tparam T Client type, constrained by Client_t.
     * @param[in] requester Authenticated caller.
     * @param[in] _id Id of the client whose services are requested.
     * @param[out] error Receives denial/error details.
     * @return True if the operation is permitted.
     */
    template <Client_t T>
    bool canGetServices(const Requester& requester,
                        uint64_t         _id,
                        Http::Error&     error);

    /**
     * @brief Coroutine form of canGetServices().
     * @tparam T Client type, constrained by Client_t.
     * @param[in] requester Authenticated caller.
     * @param[in] _id Id of the client whose services are requested.
     * @param[out] error Receives denial/error details.
     * @return True if the operation is permitted.
     * @note Callers must drive the returned task.
     */
    template <Client_t T>
    drogon::Task<bool> canGetServicesAsync(const Requester& requester,
                                           uint64_t         _id,
                                           Http::Error&     error);

    /**
     * @brief Classifies a request for abuse (whitelist/blacklist/rate/ban)
     * via the shared detector.
     * @details Delegates to DOSDetector::is_dos_attack with the shared
     * detector instance. The RateLimit filter maps the returned status to
     * an HTTP response.
     * @param[in] request IP, method, path, header hash and body captured by
     * the filter.
     * @return The detector's verdict; see DOSDetector::Status.
     * @see Filters::RateLimit
     */
    DOSDetector::Status isDosAttack(const DOSDetector::Request& request);

   private:
    /**
     * @brief Parses a JSON document from @p data without throwing to the
     * caller.
     * @param[in] data Serialized JSON body.
     * @param[out] message Appended-to diagnostic text on parse failure.
     * @param[out] success Set true only when parsing succeeded.
     * @return Parsed JSON, or std::nullopt on failure.
     */
    std::optional<jsoncons::json> parse_data(std::string_view data,
                                             std::string&     message,
                                             bool&            success);
    /**
     * @brief Extracts username/password from a JSON login body.
     * @details Calls parse_data(), then reads the "username" and "password"
     * members. Missing or ill-typed fields are reported via @p message and
     * @p success.
     * @param[in] data Serialized JSON body.
     * @param[out] message Appended-to diagnostic text on failure.
     * @param[out] success Set true only when both fields were extracted.
     * @return Parsed credentials, or std::nullopt on failure.
     */
    std::optional<Types::Credentials> parse_credentials(std::string_view data,
                                                        std::string& message,
                                                        bool&        success);

    // These are shared handles, not owned copies: the Store keeps the real
    // objects alive and many request threads use them at once. GateKeeper
    // itself adds no locking; each manager is responsible for protecting
    // its own internal state.
    std::shared_ptr<SessionManager>
        sessionManager_; /**< Session cache and login/logout
                            persistence. Shared from Store; used by
                            login, logout and the authentication/session
                            helpers. */
    std::shared_ptr<TokenManager>
        tokenManager_; /**< JWT generation and validation. Shared from
                          Store; used by login and the token-validation
                          fallback. */
    std::shared_ptr<DOSDetector>
        dosDetector_; /**< Shared per-IP abuse detector. Used only by
                         isDosAttack. */
    std::shared_ptr<PermissionManager>
        permissionManager_; /**< Group/ownership permission rules. Every
                               can* helper forwards here. */
    std::shared_ptr<PasswordWorker>
        passwordWorker_; /**< Password hashing runs here, not on the request
                            threads. Login hands the slow work to this pool.
                          */
  };
}  // namespace api::v2
