/**
 * @file sessionmanager.hpp
 * @brief The login/logout flow and the in-memory store of active sessions.
 *        (Originally: "Owns the client login flow and the in-memory session
 * cache.")
 * @details This header declares SessionManager. It splits logging in into two
 *          steps so the slow password check can run on a worker thread, and it
 *          keeps active sessions in a shared in-memory cache. Sessions are
 *          identified by a "<group>_<id>" key and carry an expiry time.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * GateKeeper calls this class on every login and logout request. It is the
 * bridge between credentials (username/password) and a valid session/token.
 * Without it, the server could neither prove who a user is nor remember that
 * they are already logged in.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Sessions vs tokens:
 * https://en.wikipedia.org/wiki/Session_(computer_science)
 * - Timing attacks (why token comparison is constant-time):
 * https://en.wikipedia.org/wiki/Timing_attack
 * - C++ shared_ptr (shared ownership):
 * https://en.cppreference.com/w/cpp/memory/shared_ptr
 * - JWT: https://jwt.io/introduction
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per source file.

#include <chrono>    // Time types used for session expiry.
#include <memory>    // std::shared_ptr for the shared helper objects.
#include <optional>  // std::optional for credentials/data that may be absent.
#include <string>    // std::string for keys and messages.

#include "drogon/utils/coroutine.h"  // drogon::Task for the coroutine fetch.
// Base class with token decoding/validation helpers.
#include "gatekeeper/keeprbase/keeprbase.hpp"
// PasswordCrypt, used to verify passwords.
#include "gatekeeper/passwordcrypt/passwordcrypt.hpp"
#include "gatekeeper/types.hpp"  // Credentials and ClientLoginData structs.
#include "store/store.hpp"       // Store, used to fetch the shared helpers.
#include "utils/global/callback.hpp"  // CALLBACK_, the HTTP response callback.
// MemCache, the in-memory session store.
#include "utils/memcache/memcache.hpp"

namespace api::v2  // All version-2 API code lives here.
{

  /**
   * @class SessionManager
   * @brief Owns the client login flow and the in-memory session cache.
   * @details Login is deliberately split into fetchPasswordHash() (database
   * reads, account activation check) and completeLogin() (password
   * verification, then login-time write). GateKeeper invokes the two parts
   * separately so the expensive password check can run on a PasswordWorker; a
   * wrong password therefore never updates the stored login time. Sessions
   * live in a shared MemCache keyed by `"<group>_<id>"` and carry an expiry.
   * All operations report failures through a human-readable message (or a
   * callback for logout).
   * @see KeeprBase
   * In everyday words: logging in is like showing ID at a desk. First we look
   * up your account and check it is active; then we check your password; only
   * then do we record that you logged in and hand you a session.
   */
  class SessionManager : public KeeprBase  // Inherits token decode/validate
                                           // helpers; see KeeprBase.
  {
   public:
    SessionManager() = default;  // No extra setup needed; the base class
                                 // handles its own state.
    SessionManager(const SessionManager&) =
        default;  // Copying shared pointers is harmless.
    SessionManager(SessionManager&&) =
        delete;  // Moving is disabled; this object shares
                 // state that should not be transferred.
    SessionManager& operator=(const SessionManager&) =
        delete;  // Assignment is disabled along with moving.
    SessionManager& operator=(SessionManager&&) =
        delete;  // Move assignment is disabled for the same reason.
    ~SessionManager() override =
        default;  // Destructor cleans up through the base class interface.

    // login() = fetchPasswordHash() then completeLogin(). GateKeeper calls
    // the two parts separately so the password check can run on the
    // password worker; a wrong password never updates the login time. This
    // keeps the expensive, deliberate slowness of password hashing away
    // from the web server's request threads.
    /**
     * @brief Runs the full login synchronously on the calling thread: fetch
     * then complete.
     * @param[in] credentials Client username/password.
     * @param[in,out] clientLoginData Populated with client id, username and
     * active state.
     * @param[out] message Human-readable failure reason.
     * @return True only when the password matches and the login time was
     * recorded.
     */
    bool login(const std::optional<Types::Credentials>& credentials,
               std::optional<Types::ClientLoginData>&   clientLoginData,
               std::string& message);  // Runs both login steps on the calling
                                       // thread; true on success.

    /**
     * @brief Reads the stored password hash and checks that the account is
     * active (no writes).
     * @param[in] credentials Credentials whose username selects the
     * account.
     * @param[in,out] clientLoginData Receives id, username and active flag.
     * @param[out] message Human-readable failure reason.
     * @return The stored hash on success, `std::nullopt` otherwise.
     * @throws DatabaseBusy Re-thrown so callers can retry/back off.
     */
    std::optional<std::string>
    fetchPasswordHash(  // Step 1: read and return the stored hash
                        // (declaration continues next line).
        const std::optional<Types::Credentials>& credentials,
        std::optional<Types::ClientLoginData>&   clientLoginData,
        std::string& message);  // Credentials go in; client data and a
                                // failure message come out.

    /**
     * @brief Coroutine form of fetchPasswordHash(); awaits the async
     * database read.
     * @param[in] credentials Supplied username/password and group.
     * @param[in,out] clientLoginData Receives id, username and active flag.
     * @param[out] message Human-readable failure reason.
     * @return The stored hash on success, `std::nullopt` otherwise.
     * @throws DatabaseBusy Re-thrown from the coroutine when awaited.
     */
    drogon::Task<std::optional<std::string>>
    fetchPasswordHashCoro(  // Coroutine form of step 1 (declaration
                            // continues next line).
        const std::optional<Types::Credentials>& credentials,
        std::optional<Types::ClientLoginData>&   clientLoginData,
        std::string& message);  // Credentials go in; client data and a
                                // failure message come out.

    /**
     * @brief Verifies @p password_hash and, only on success, records the
     * new login time.
     * @param[in] credentials Supplied username/password and group.
     * @param[in] password_hash Hash previously returned by
     * fetchPasswordHash().
     * @param[in,out] clientLoginData Identity/state accumulator.
     * @param[out] message Human-readable failure reason.
     * @return True when the password matches and the login-time update
     * succeeded.
     * @throws DatabaseBusy Re-thrown so callers can retry/back off.
     */
    bool completeLogin(
        const std::optional<Types::Credentials>& credentials,
        const std::string&
            password_hash,  // Step 2: verify the password, then record
                            // login (signature continues next line).
        std::optional<Types::ClientLoginData>& clientLoginData,
        std::string& message);  // Client data and the failure message are
                                // passed back through these arguments.

    /**
     * @brief Invalidates the client's token/session and invokes @p callback
     * with the result.
     * @details Validates the JWT, then compares it to any stored session
     * token in constant time before recording logout time and removing the
     * session. All outcomes are delivered through @p callback (moved
     * exactly once).
     * @param[in,out] callback Callback invoked with status and JSON body.
     * @param[in,out] clientLoginData Client identity including the token.
     */
    void logout(CALLBACK_&& callback,
                std::optional<Types::ClientLoginData>&
                    clientLoginData);  // Ends the session and reports the
                                       // result through the callback.

    /**
     * @brief Checks a session against the cache and verifies the token's
     * identity data.
     * @details A constant-time comparison with the stored token prevents a
     * forged JWT with a valid (group, id) pair from hijacking a session; on
     * success the stored entry replaces the supplied data so its
     * identity/expiry are trusted.
     * @param[in,out] clientLoginData Identity/state accumulator, replaced
     * by the stored session on success.
     * @param[out] message Human-readable failure reason.
     * @return True when the cached session and token agree.
     */
    bool clientHasValidSession(
        std::optional<Types::ClientLoginData>& clientLoginData,
        std::string& message);  // Checks the cached session against the
                                // token; true when they agree.

    /**
     * @brief Inserts the client's session into the cache with a TTL taken
     * from `expireTime`.
     * @param[in,out] clientLoginData Session data; keyed by group and
     * client id.
     * @param[out] message Human-readable failure reason.
     * @return True on insertion.
     * @note The TTL forwarded to MemCache::insert is `expireTime` — the JWT
     * `exp` claim as a `system_clock` duration since the epoch (set by
     * decodeToken), not an interval remaining until expiry. Consequently
     * the effective cache lifetime is that absolute value rather than a
     * short time-until-expiry.
     */
    bool storeSession(
        std::optional<Types::ClientLoginData>& clientLoginData,
        std::string& message);  // Adds the session to the in-memory cache.

    /**
     * @brief Removes the client's session from the cache (keyed by group
     * and client id).
     * @return True on removal; false when the data/id/group are missing or
     * an error occurs.
     */
    bool removeSession(
        std::optional<Types::ClientLoginData>&
            clientLoginData);  // Deletes the session from the cache.

   private:  // Implementation details; callers use the public methods above.
    static constexpr std::chrono::seconds SESSION_TIMEOUT =
        std::chrono::seconds(3600); /**< Default session lifetime: one hour,
                                       used if no other TTL is given. */
    static constexpr int SESSION_MAX =
        4096; /**< Cache capacity: at most this many sessions are held. */

    std::shared_ptr<PasswordCrypt> passwordCrypt =
        Store::getObject<PasswordCrypt>(); /**< Shared hasher/verifier,
                                              reused rather than recreated
                                              per login. */
    std::shared_ptr<MemCache<Types::ClientLoginData>> clientsSessionsList =
        Store::getObject<MemCache<
            Types::ClientLoginData>>(SESSION_MAX,
                                     SESSION_TIMEOUT); /**< Shared session
                                                          store, holding
                                                          ClientLoginData by
                                                          "<group>_<id>" key. */
  };
}  // namespace api::v2
