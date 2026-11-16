/**
 * @file keeprbase.hpp
 * @brief Shared base for session/token managers offering session-table and JWT
 * helpers.
 * @details This header declares KeeprBase, a base class holding the database
 * and JWT code that both SessionManager and TokenManager need. It can read and
 * update session rows, format UTC timestamps, fetch password hashes, and
 * decode/verify JWTs. It keeps a cached copy of the token parameters (including
 * the signing secret) and reaches the database through a shared
 * DatabaseController, so derived managers reuse the same connection pool rather
 * than opening their own.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * SessionManager and TokenManager both need to touch the session tables and
 * both need to reason about JWTs. Instead of duplicating that SQL and JWT
 * logic, they inherit these helpers from KeeprBase. GateKeeper calls those
 * managers, which call these helpers. Without this shared base, the same
 * session-table mapping and token-verification rules would be copy-pasted in
 * two places and could drift apart. The class is abstract-ish (meant to be
 * inherited), and its members are protected so only derived managers can use
 * them.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Inheritance / base class:
 * https://en.cppreference.com/w/cpp/language/derived_class
 * - JWT and HS256 signing: https://jwt.io/introduction
 * - SQL prepared / parameterized statements (avoiding SQL injection):
 * https://en.wikipedia.org/wiki/Prepared_statement
 * - std::optional ("a value that may be missing"):
 * https://en.cppreference.com/w/cpp/utility/optional
 * - Coroutine / co_await: https://en.cppreference.com/w/cpp/language/coroutines
 * - Singleton / service locator:
 * https://en.wikipedia.org/wiki/Singleton_pattern
 * - RAII (resources tied to object lifetime):
 * https://en.cppreference.com/w/cpp/language/raii
 * - [[nodiscard]] (compiler warns if the return value is ignored):
 * https://en.cppreference.com/w/cpp/language/attributes
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include once per translation unit.
// Core jwt-cpp types: decoded_jwt, verify, verifier, algorithms.
#include <jwt-cpp/jwt.h>
// The picojson JSON traits used by the JWT library here.
#include <jwt-cpp/traits/kazuho-picojson/traits.h>

#include <cstdint>  // uint64_t for client ids.
// jsoncons::json returned by the password-hash helpers.
#include <jsoncons/basic_json.hpp>
#include <memory>    // std::shared_ptr to Configurator and DatabaseController.
#include <optional>  // std::optional results and members.
#include <string>    // std::string for timestamps, groups and messages.

// Configurator and TokenManagerParameters.
#include "configurator/configurator.hpp"
// DatabaseController, used for all SQL.
#include "controllers/databasecontroller/databasecontroller.hpp"
#include "drogon/utils/coroutine.h"  // drogon::Task coroutine return type.
// Types::ClientLoginData passed through the helpers.
#include "gatekeeper/types.hpp"
#include "store/store.hpp"  // Store::getObject / cachedObject.

namespace api::v2  // API version 2 namespace.
{
  /**
   * @class KeeprBase
   * @brief Shared base for session/token managers offering session-table and
   * JWT helpers.
   * @details KeeprBase centralizes the SQL and JWT logic used by both
   * SessionManager and TokenManager: reading/updating session rows,
   * formatting UTC timestamps, decoding and verifying JWTs, and loading
   * password hashes. It is stateless apart from a cached copy of
   * TokenManagerParameters; database access goes through a cached
   *          DatabaseController obtained from Store, so instances share the
   * same pool.
   * @note Assignment is disabled because the token parameters are cached at
   * construction; copying/moving is allowed so derived managers can use
   * normal value semantics.
   * @see SessionManager, TokenManager
   * @details For a beginner: this is a "toolbox base class". The two managers
   * inherit it so they automatically have all these helpers. It caches the
   * token settings once at construction, which is why assignment is removed -
   * assigning could silently replace those cached settings. Copy/move
   * construction is fine because it just carries the settings and the shared
   * config handle along.
   */
  class KeeprBase
  {
   public:
    /**
     * @brief Caches the configurator and its token parameters from the
     * Store singleton.
     * @details Grabbing a shared Configurator and copying its
     * TokenManagerParameters once avoids repeated lookups and freezes the
     * signing settings for this object.
     */
    KeeprBase()
        : configurator_(Store::getObject<Configurator>()),
          tokenManagerParameters_(
              configurator_->get<Configurator::TokenManagerParameters>())
    {
    }  // Fetch the shared config, then copy the token settings out of it.
    KeeprBase(const KeeprBase& other) =
        default;  // Copy is fine: it copies the cached settings and shared
                  // handle.
    KeeprBase& operator=(const KeeprBase& other) =
        delete;  // No copy assignment: the cached parameters should not be
                 // overwritten.
    KeeprBase(KeeprBase&& other) noexcept =
        default;  // Move is fine and never throws.
    KeeprBase& operator=(KeeprBase&& other) noexcept =
        delete;  // No move assignment for the same reason.
    /**
     * @brief Virtual destructor; derived managers are destroyed through
     * this base.
     */
    virtual ~KeeprBase() = default;  // Virtual so deleting a derived manager
                                     // through a KeeprBase* is safe.

   protected:  // Helpers are visible to derived managers only, not to
               // outside callers.
    /**
     * @brief Records a login timestamp and returns the previously stored
     * logout time.
     * @details Upserts the client's session row (`last_login`, keeping
     * `last_logout`) and writes the new logout time back into @p
     * clientLoginData.
     * @param[in,out] clientLoginData Supplies group/clientId; receives
     * nowLoginTime and lastLogoutTime.
     * @return True if the row was updated; false for an unknown group,
     * missing fields or DB error.
     * @throws DatabaseBusy Propagated so callers can apply their
     * retry/back-pressure policy.
     */
    bool setNowLoginTimeGetLastLogoutTime(
        std::optional<Types::ClientLoginData>&
            clientLoginData);  // An "upsert" inserts a new row or updates
                               // the existing one.
    /**
     * @brief Loads the account's active flag and last logout time from the
     * session/client tables.
     * @details Populates `is_active` and `lastLogoutTime` in @p
     * clientLoginData.
     * @param[in,out] clientLoginData Supplies group/clientId; receives
     * active/logout state.
     * @return True when a session row was found.
     * @throws DatabaseBusy Propagated to callers.
     */
    bool getLastLogoutTimeIfActive(
        std::optional<Types::ClientLoginData>&
            clientLoginData);  // Used before trusting a token, to check for
                               // a later logout/suspension.
    /**
     * @brief Formats the current UTC time as a fixed-width string with a
     * +0000 offset.
     * @return Timestamp string suitable for storage and JWT claims.
     * @throws std::runtime_error If the UTC conversion fails.
     */
    static std::string
    current_time_to_utc_string();  // Static because it needs no object
                                   // state; returns text in the format used
                                   // by the DB.
    /**
     * @brief Upserts the client's `last_logout` timestamp.
     * @param[in] _id Client id.
     * @param[in] _group Client group selecting the session table.
     * @note Failures are logged; this helper does not report success to the
     * caller.
     * @throws DatabaseBusy Propagated to callers.
     */
    void setNowLogoutTime(
        uint64_t           _id,
        const std::string& _group);  // Writing this time invalidates tokens
                                     // issued before the logout.
    /**
     * @brief Reads the stored `last_login` timestamp for a client.
     * @param[in] _id Client id.
     * @param[in] _group Client group selecting the session table.
     * @return The timestamp, or std::nullopt for an unknown group or
     * missing row.
     */
    std::optional<std::string> getLastLoginTime(
        uint64_t _id,
        const std::string&
            _group);  // Optional because there may be no session row.
    /**
     * @brief Fetches the stored password hash for a username via
     * DatabaseController.
     * @param[in] username User name to look up.
     * @param[in] _group Client group selecting the account table.
     * @return JSON result from storage; content/shape are defined by
     * DatabaseController.
     */
    std::optional<jsoncons::json> getPasswordHashForUserName(
        const std::string& username,
        const std::string&
            _group);  // Simply forwards to the database controller.
    /**
     * @brief Coroutine form of getPasswordHashForUserName().
     * @param[in] username User name to look up.
     * @param[in] _group Client group selecting the account table.
     * @return JSON result from storage.
     * @note Callers must drive the returned task; the frame must outlive
     * the awaited query.
     */
    drogon::Task<std::optional<jsoncons::json>> getPasswordHashForUserNameCoro(
        const std::string& username,
        const std::string& _group);  // Non-blocking variant for IO threads.
    /**
     * @brief Extracts identity claims from an already decoded JWT and
     * checks expiry/IP binding.
     * @details Populates expireTime, group, clientId and username in @p
     * clientLoginData.
     * @param[in,out] clientLoginData Receives the decoded claim values.
     * @param[out] message Receives a failure reason ("Token expired" or IP
     * mismatch).
     * @param[in,out] decoedToken Decoded JWT whose payload claims are read.
     * @return True if claims are present, unexpired and IP-bound.
     * @throws DatabaseBusy Propagated to callers.
     */
    static bool decodeToken(
        std::optional<Types::ClientLoginData>& clientLoginData,
        std::string&                           message,
        jwt::decoded_jwt<jwt::traits::kazuho_picojson>&
            decoedToken);  // Reads claims but does NOT verify the
                           // signature.
    /**
     * @brief Validates a decoded JWT against current session state and its
     * signature/claims.
     * @details Checks the "llodt" logout claim against the stored value,
     * the account active flag, then verifies the token with
     * createTokenVerifier().
     * @param[in,out] clientLoginData Identity/state accumulator; session
     * fields are refreshed.
     * @param[out] message Failure reason.
     * @param[in,out] decodedToken Token to verify.
     * @return True if the token is still valid.
     * @throws DatabaseBusy Propagated to callers.
     */
    bool validateToken(std::optional<Types::ClientLoginData>& clientLoginData,
                       std::string&                           message,
                       jwt::decoded_jwt<jwt::traits::kazuho_picojson>&
                           decodedToken);  // Full verification: logout state,
                                           // active flag, signature.
    /**
     * @brief Coroutine form of getLastLogoutTimeIfActive().
     * @param[in,out] clientLoginData Supplies group/clientId; receives
     * active/logout state.
     * @return True when a session row was found.
     * @note Callers must drive the returned task; the frame must outlive
     * the awaited query.
     * @throws DatabaseBusy Propagated to callers.
     */
    drogon::Task<bool> getLastLogoutTimeIfActiveCoro(
        std::optional<Types::ClientLoginData>&
            clientLoginData);  // Awaits the DB read instead of blocking.
    /**
     * @brief Coroutine form of validateToken().
     * @details Awaits the session-state read, then performs the same
     * logout/active/verifier checks.
     * @param[in,out] clientLoginData Identity/state accumulator.
     * @param[out] message Failure reason.
     * @param[in,out] decodedToken Token to verify.
     * @return True if the token is still valid.
     * @note Callers must drive the returned task; the frame must outlive
     * the awaited query.
     * @throws DatabaseBusy Propagated to callers.
     */
    drogon::Task<bool> validateTokenCoro(
        std::optional<Types::ClientLoginData>& clientLoginData,
        std::string&                           message,
        jwt::decoded_jwt<jwt::traits::kazuho_picojson>&
            decodedToken);  // Coroutine variant of validateToken.
    /**
     * @brief Builds an HS256 verifier bound to the configured secret and
     * the client's claims.
     * @param[in] clientLoginData Must contain username, clientId, group,
     * ip_address and lastLogoutTime.
     * @return Configured verifier; the caller applies it to a decoded
     * token.
     * @throws std::runtime_error If username, clientId or group is absent
     * (explicitly checked).
     * @throws std::bad_optional_access If ip_address or lastLogoutTime is
     * absent; those two claims are read with `.value()` without a prior
     * presence check.
     */
    [[nodiscard]] jwt::verifier<jwt::default_clock,
                                jwt::traits::kazuho_picojson>
    createTokenVerifier(
        const std::optional<Types::ClientLoginData>& clientLoginData)
        const;  // nodiscard: ignoring the verifier would mean no check is
                // done.
    /**
     * @brief Returns a copy of the token parameters cached at construction.
     * @return Cached TokenManagerParameters (including the signing secret).
     * @warning The returned parameters contain the JWT secret; do not
     * expose them to clients.
     */
    [[nodiscard]] Configurator::TokenManagerParameters
    getTokenManagerParameters() const
    {
      return tokenManagerParameters_;
    }  // Returns by value so callers cannot mutate the cached copy.

    /**
     * @brief Returns the shared DatabaseController from the Store cache.
     * @return Shared controller used by all helpers in this class.
     */
    [[nodiscard]] std::shared_ptr<DatabaseController> databaseController() const
    {
      return Store::cachedObject<DatabaseController>();
    }  // Thread-local cached lookup; cheap on the request path.

   private:
    std::shared_ptr<Configurator>
        configurator_; /**< Owned handle keeping the configuration source
                          alive. Shared from Store. */
    Configurator::TokenManagerParameters
        tokenManagerParameters_; /**< Cached copy of JWT signing/validation
                                    parameters. Includes the secret; never
                                    log it. */
  };
}  // namespace api::v2
