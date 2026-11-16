/**
 * @file keeprbase.cpp
 * @brief Implementation for keeprbase: Maps a client group to its session table
 * via a fixed allowlist.
 * @details This file implements the shared SQL and JWT helpers used by the
 * session and token managers. It chooses a session table from a hard-coded
 * allowlist of the two valid groups (`users` and `providers`), then builds
 * parameterized SQL to read or update login/logout times and to load password
 * hashes. It also formats UTC timestamps, extracts identity claims from decoded
 * JWTs, checks them against live session state, and builds an HS256 verifier
 * bound to the configured secret. Most helpers have a coroutine twin that
 * awaits the database read instead of blocking a thread.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the persistence and token-verification engine under the gatekeeper.
 * SessionManager and TokenManager call these helpers during login, logout and
 * every token check; those managers are called by GateKeeper, which is called
 * by controllers and filters. Crucially, mapping a group to a table through an
 * allowlist (instead of pasting user input into SQL) prevents SQL injection,
 * and parameterized queries keep the actual values safe too. Without this file
 * the managers would have no way to record logouts or decide whether an old
 * token has been invalidated.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - SQL injection and why parameterized queries / allowlists prevent it:
 * https://en.wikipedia.org/wiki/SQL_injection
 * - Prepared statements: https://en.wikipedia.org/wiki/Prepared_statement
 * - CTE (the WITH clause used in one query):
 * https://www.postgresql.org/docs/current/queries-with.html
 * - UPSERT (INSERT ... ON CONFLICT DO UPDATE):
 * https://www.postgresql.org/docs/current/sql-insert.html
 * - JWT / HS256 signature verification: https://jwt.io/introduction
 * - std::optional: https://en.cppreference.com/w/cpp/utility/optional
 * - Coroutine / co_await: https://en.cppreference.com/w/cpp/language/coroutines
 * - gmtime_r (thread-safe UTC time conversion):
 * https://en.cppreference.com/w/c/chrono/gmtime
 * - Anonymous namespace (file-private helper):
 * https://en.cppreference.com/w/cpp/language/namespace
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// The interface implemented here.
#include "gatekeeper/keeprbase/keeprbase.hpp"

// fmt::format builds SQL strings and diagnostic messages.
#include <fmt/core.h>
#include <jwt-cpp/jwt.h>  // jwt::verify, decoded_jwt, verifier and exceptions.
// The picojson traits used with jwt-cpp here.
#include <jwt-cpp/traits/kazuho-picojson/traits.h>

#include <chrono>     // system_clock used to get the current time.
#include <cstdint>    // uint64_t client ids.
#include <ctime>      // std::tm and gmtime_r for UTC conversion.
#include <exception>  // std::exception caught by the helpers.
// jsoncons::json returned by password-hash helpers.
#include <jsoncons/basic_json.hpp>
#include <optional>   // std::optional results.
#include <pqxx/pqxx>  // pqxx::params, the parameter container for SQL queries.
// std::runtime_error thrown when time conversion or verifier input is bad.
#include <stdexcept>
#include <string>  // std::string for SQL and timestamps.

// DatabaseBusy exception, rethrown so callers can back off.
#include "database/databasebusy.hpp"
#include "gatekeeper/types.hpp"     // Types::ClientLoginData.
#include "utils/global/global.hpp"  // CRITICALMESSAGE logging macro.
// Message::ErrorMessage / CriticalMessage.
#include "utils/message/message.hpp"
using KeeprBase =
    api::v2::KeeprBase;  // Short alias for the class name used below.

namespace  // Anonymous namespace: sessionsTable is private to this .cpp file.
{
  /**
   * @brief Maps a client group to its session table via a fixed allowlist.
   * @details Table names cannot be bound as query parameters, so
   * interpolation is restricted to the two known groups; any other value is
   * rejected instead of interpolated.
   * @param[in] group Client group string.
   * @return The table name, or std::nullopt for an unrecognized group.
   * @details SQL placeholders (like $1) can only stand for *values*, not for
   * identifiers such as table names. So this function never pastes the
   * caller's string directly into a query: it only accepts the two exact
   * known groups and builds a safe table name. Anything else returns nullopt,
   * and callers reject the request.
   */
  // Identifiers cannot be bound as parameters, so the session table is chosen
  // from a fixed allowlist.
  std::optional<std::string> sessionsTable(const std::string& group)
  {
    if (group == "users" ||
        group == "providers")  // Only these two groups are allowed.
    {
      return group + "_sessions";  // Build "users_sessions" /
                                   // "providers_sessions" from the known
                                   // safe value.
    }
    return std::nullopt;  // Unknown group: refuse (never interpolate
                          // untrusted text).
  }
}  // namespace

/**
 * Loads the account's active flag and last logout time from storage.
 * Joins the session and client tables to populate `is_active` and
 * `lastLogoutTime`. A DatabaseBusy exception is rethrown so callers can decide
 * how to back off. clientLoginData: Supplies group/clientId; receives the
 * loaded state. Returns True when a session row was found. Throws DatabaseBusy:
 * When the database is busy.
 */
bool KeeprBase::getLastLogoutTimeIfActive(
    std::optional<Types::ClientLoginData>& clientLoginData)
{
  try
  {
    const auto sessions = sessionsTable(
        clientLoginData->group.value());  // Resolve the table name from the
                                          // group (allowlisted).
    if (!sessions.has_value())  // Unknown group -> cannot build a safe query.
    {
      Message::ErrorMessage("Unknown client group.");  // Log the bad group.
      return false;                                    // Fail: no state loaded.
    }
    const std::string param_query = fmt::format(
        "WITH client_data AS (SELECT ses.id, ses.last_logout, "
        "client.active FROM {} ses "
        "LEFT JOIN {} client ON ses.id = client.id) "
        "SELECT id, last_logout, active FROM client_data WHERE id = $1;",
        sessions.value(),
        clientLoginData->group
            .value());    // Fill the two {} placeholders with the safe table
                          // names; $1 stays a bound parameter.
    pqxx::params params;  // Holds values for the $1 placeholder.
    params.append(
        clientLoginData->clientId
            .value());  // Bind the client id as a value, not as SQL text.

    auto results =
        databaseController()
            ->executeReadQuery(param_query,
                               params);  // Run the read query through the
                                         // shared controller.

    if (results.has_value())  // Did the query return a result set at all?
    {
      if (!results->empty())  // Did it find a matching row?
      {
        clientLoginData->is_active =
            results.value()
                .at("active")
                .as_bool();  // Fill the active flag from the row.
        clientLoginData->lastLogoutTime =
            results.value()
                .at("last_logout")
                .as_string();  // Fill the last logout time.
        return true;           // State loaded successfully.
      }
    }
    return false;  // No result or no row: nothing was loaded.
  }
  catch (const DatabaseBusy&)  // Database is overloaded.
  {
    throw;  // Rethrow so the caller can apply its retry/back-pressure
            // policy.
  }
  catch (std::exception& e)  // Any other unexpected error.
  {
    CRITICALMESSAGE  // Log it with function/file/line details.
  }
  return false;  // Failed to load state.
}

/**
 * Upserts the session row with the current login time and returns the old
 * logout time. Initializing both `last_login` and `last_logout` to the new
 * login time on insert gives a fresh session a consistent "llodt" claim. On
 * conflict only `last_login` changes, so an existing logout marker survives
 * until the next explicit logout. clientLoginData: Supplies group/clientId;
 * receives nowLoginTime and lastLogoutTime. Returns True when the row was
 * upserted. Throws DatabaseBusy: When the database is busy.
 */
bool KeeprBase::setNowLoginTimeGetLastLogoutTime(
    std::optional<Types::ClientLoginData>& clientLoginData)
{
  try
  {
    clientLoginData->nowLoginTime =
        current_time_to_utc_string();  // Generate the timestamp for this
                                       // login.

    if (!clientLoginData->group.has_value() ||
        !clientLoginData->clientId
             .has_value())  // Both are needed to build the row.
    {
      Message::ErrorMessage(
          "group or client id is empty");  // Log the missing input.
      return false;                        // Cannot proceed.
    }
    const auto sessions = sessionsTable(
        clientLoginData->group.value());  // Resolve the table name safely.
    if (!sessions.has_value())            // Unknown group.
    {
      // Log it.
      Message::ErrorMessage("Unknown client group.");
      return false;  // Refuse.
    }
    const std::string param_query = fmt::format(
        "INSERT INTO {} (id, last_login, last_logout) VALUES ($1, $2, $2) "
        "ON CONFLICT (id) DO UPDATE SET last_login = EXCLUDED.last_login "
        "RETURNING last_logout;",
        sessions.value());  // Only the table name is filled in; $1/$2
                            // remain bound parameters.
    pqxx::params params;    // Parameter container.
    params.append(clientLoginData->clientId.value());  // $1: the client id.
    params.append(clientLoginData->nowLoginTime
                      .value());  // $2: the new login time (also used for
                                  // last_logout on insert).

    auto result =
        databaseController()->executeQuery(param_query,
                                           params);  // Run the write query.

    if (!result.has_value())  // Query failed to return a result.
    {
      Message::ErrorMessage("Error updating login time.");  // Log the failure.
      return false;                                         // Report failure.
    }

    if (!result->empty())  // The RETURNING clause gave back the old logout
                           // time.
    {
      clientLoginData->lastLogoutTime =
          result->at("last_logout")
              .as_string();  // Save it for the token's "llodt" claim.
    }
    return true;  // Upsert succeeded.
  }
  catch (const DatabaseBusy&)  // Database overloaded.
  {
    throw;  // Let the caller back off.
  }
  catch (const std::exception& e)  // Other unexpected error.
  {
    // Log context.
    Message::ErrorMessage("Error updating login time.");
    Message::CriticalMessage(e.what());  // Log details.
  }
  return false;  // Report failure.
}

/**
 * Upserts the client's `last_logout` timestamp, used to invalidate older JWTs.
 * Failures are logged only; the caller receives no status. DatabaseBusy is
 * rethrown. _id: Client id. _group: Client group selecting the session table.
 * Throws DatabaseBusy: When the database is busy.
 */
void KeeprBase::setNowLogoutTime(uint64_t _id, const std::string& _group)
{
  try
  {
    std::string logout_time =
        current_time_to_utc_string();  // Timestamp marking "logged out at".

    const auto sessions =
        sessionsTable(_group);  // Resolve the table name from the group.
    if (!sessions.has_value())  // Unknown group.
    {
      // Log it.
      Message::ErrorMessage("Unknown client group.");
      return;  // Nothing to do.
    }
    const std::string param_query = fmt::format(
        "INSERT INTO {} (id, last_logout) VALUES ($1, $2) "
        "ON CONFLICT (id) DO UPDATE SET last_logout = "
        "EXCLUDED.last_logout;",
        sessions.value());       // Upsert only the logout column, preserving
                                 // last_login.
    pqxx::params params;         // Parameter container.
    params.append(_id);          // $1: client id.
    params.append(logout_time);  // $2: the logout timestamp.

    auto result = databaseController()->executeQuery(param_query,
                                                     params);  // Run the write.

    if (!result.has_value())  // No result returned.
    {
      Message::CriticalMessage(
          "Error updating login time.");  // Log a critical failure
                                          // (message text says "login"
                                          // though this is logout).
    }
  }
  catch (const DatabaseBusy&)  // Database overloaded.
  {
    throw;  // Let the caller back off.
  }
  catch (const std::exception& e)  // Other error.
  {
    // Log context.
    Message::ErrorMessage("Error updating logout time.");
    Message::CriticalMessage(e.what());  // Log details.
  }
}

/**
 * @brief Reads the stored `last_login` timestamp for a client.
 * @param[in] _id Client id.
 * @param[in] _group Client group selecting the session table.
 * @return The timestamp, or std::nullopt for an unknown group or missing row.
 */
std::optional<std::string> KeeprBase::getLastLoginTime(
    uint64_t _id, const std::string& _group)
{
  const auto sessions = sessionsTable(_group);  // Resolve the safe table name.
  if (!sessions.has_value())                    // Unknown group.
  {
    Message::ErrorMessage("Unknown client group.");  // Log it.
    return std::nullopt;                             // No timestamp available.
  }
  const std::string param_query =
      fmt::format("SELECT last_login FROM {} WHERE id = $1;",
                  sessions.value());  // Safe table name, bound id value.
  pqxx::params params;                // Parameter container.
  params.append(_id);                 // Bind the client id as $1.

  return databaseController()
      ->doReadQuery(param_query,
                    params);  // Return the controller's optional result
                              // directly.
}

/**
 * @brief Formats the current wall-clock time as a fixed-width UTC timestamp
 * string.
 * @details Uses gmtime_r for thread-safe UTC conversion, then formats as
 *          "YYYY-MM-DD HH:MM:SS +0000" to match the storage and JWT claim
 * format.
 * @return The formatted timestamp.
 * @throws std::runtime_error If gmtime_r fails.
 */
std::string KeeprBase::current_time_to_utc_string()
{
  constexpr int YEAR_OFFSET = 1900;  // std::tm stores years since 1900, so
                                     // add this to get a real year.
  // Get current time as a time_point
  auto now = std::chrono::system_clock::now();  // Current wall-clock time.

  // Convert time_point to time_t
  auto now_t =
      std::chrono::system_clock::to_time_t(now);  // C-style time for gmtime_r.

  // Convert time_t to tm structure in UTC
  std::tm mtime{};  // Zero-initialized broken-down time structure.
  if (gmtime_r(&now_t, &mtime) ==
      nullptr)  // Thread-safe UTC conversion; returns null on failure.
  {
    throw std::runtime_error(
        "Failed to convert time to UTC");  // Fail loudly rather than
                                           // returning a bogus time.
  }

  // Format the time using fmt::format
  std::string formatted_time =
      fmt::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02} +0000",
                  mtime.tm_year + YEAR_OFFSET,
                  mtime.tm_mon + 1,
                  mtime.tm_mday,
                  mtime.tm_hour,
                  mtime.tm_min,
                  mtime.tm_sec);  // Zero-padded, fixed-width UTC with +0000
                                  // offset; tm_mon is 0-based so add 1.

  return formatted_time;  // Hand back the text.
}

/**
 * Extracts identity claims from a decoded JWT and checks expiry and IP binding.
 * Rejects expired tokens and tokens whose "ip_address" claim differs from the
 *          request IP, then fills in group, clientId and username. This helper
 * reads claims but does not verify the signature; use validateToken() for full
 * verification. clientLoginData: Receives expireTime and the identity claims.
 * message: Failure reason.
 * decoedToken: Decoded JWT to read claims from.
 * Returns True when claims are well formed, unexpired and IP-bound.
 * Throws DatabaseBusy: When the database is busy.
 */
bool KeeprBase::decodeToken(
    std::optional<Types::ClientLoginData>&          clientLoginData,
    std::string&                                    message,
    jwt::decoded_jwt<jwt::traits::kazuho_picojson>& decoedToken)
{
  try
  {
    // Validate token expiration
    auto now = std::chrono::system_clock::now()
                   .time_since_epoch();  // Current time as a duration since
                                         // the epoch.
    clientLoginData->expireTime =
        decoedToken.get_payload_claim("exp")
            .as_date()
            .time_since_epoch();  // Read the "exp" claim as a duration.

    if (now >= clientLoginData->expireTime)  // Current time at or past expiry.
    {
      message = "Token expired";  // Explain why.
      return false;               // Reject.
    }

    // Bind the token to the originating IP; a copied token from elsewhere
    // is rejected.
    if (clientLoginData->ip_address !=
        decoedToken.get_payload_claim("ip_address")
            .as_string())  // IP claim must equal the request IP.
    {
      message =
          // Explain the mismatch.
          "Token ip address does not match user ip address";
      return false;  // Reject a stolen/replayed token.
    }

    clientLoginData->group    = decoedToken.get_payload_claim("group")
                                    .as_string();  // Read the group claim.
    clientLoginData->clientId = std::stoull(
        decoedToken.get_id());  // Convert the "jti"/id claim text to a number.
    clientLoginData->username =
        decoedToken.get_subject();  // Read the subject (username) claim.

    return true;  // Claims look good (signature checked separately).
  }
  catch (const DatabaseBusy&)  // Database overloaded.
  {
    throw;  // Let the caller back off.
  }
  catch (
      const std::exception& e)  // Missing/malformed claim or conversion error.
  {
    message = fmt::format("Error storing session:{}",
                          e.what());    // Build a diagnostic message.
    Message::CriticalMessage(message);  // Log it.
  }
  return false;  // Reject on error.
}
/**
 * Verifies a decoded JWT against live session state and its signature/claims.
 * The "llodt" claim is compared with the stored last logout time so tokens
 * issued before a logout are invalidated, and suspended accounts are rejected.
 * The return value of the session read is not consulted here; the loaded fields
 * are used directly. clientLoginData: Identity/state accumulator; session
 * fields are refreshed. message: Failure reason. decodedToken: Token to verify.
 * Returns True if the token remains valid.
 * Throws DatabaseBusy: When the database is busy.
 */
bool KeeprBase::validateToken(
    std::optional<Types::ClientLoginData>&          clientLoginData,
    std::string&                                    message,
    jwt::decoded_jwt<jwt::traits::kazuho_picojson>& decodedToken)
{
  try
  {
    // Refresh logout/active state; the token's "llodt" claim must still
    // match.
    getLastLogoutTimeIfActive(clientLoginData);  // Reload last_logout and
                                                 // is_active from the database.
    if (clientLoginData->lastLogoutTime !=
        decodedToken.get_payload_claim("llodt")
            .as_string())  // Did a logout happen after this token was
                           // issued?
    {
      message =
          // Explain.
          "Client is logged out from server, Please login again.";
      return false;  // Reject the stale token.
    }

    if (!clientLoginData->is_active)  // Account suspended.
    {
      message =
          // Explain.
          "Account is suspend, please contact the administrator.";
      return false;  // Reject.
    }

    // Validate token claims
    auto verifier =
        createTokenVerifier(clientLoginData);  // Build a verifier bound to the
                                               // secret and identity claims.

    verifier.verify(
        decodedToken);  // Verify signature + issuer/type/subject/id/claims;
                        // throws on failure.

    // Token is valid
    return true;  // All checks passed.
  }
  catch (const jwt::error::token_verification_exception&
             e)  // Signature or claim verification failed.
  {
    message =
        // Explain.
        fmt::format("Token verification failed:{}", e.what());
    Message::CriticalMessage(message);  // Log it.
    return false;                       // Reject.
  }
  catch (const DatabaseBusy&)  // Database overloaded.
  {
    throw;  // Let the caller back off.
  }
  catch (const std::exception& e)  // Other unexpected error.
  {
    message =
        // Explain.
        fmt::format("Error validating Token:{}", e.what());
    Message::CriticalMessage(message);  // Log it.
    return false;                       // Reject.
  }
  return false;  // Defensive fallback.
}

/**
 * Coroutine form of getLastLogoutTimeIfActive(), awaiting the database read.
 * Executes on the Drogon executor that drives the returned task; the frame
 * remains alive across the co_await. DatabaseBusy is rethrown; other exceptions
 * yield false. clientLoginData: Supplies group/clientId; receives active/logout
 * state. Returns True when a session row was found. Throws DatabaseBusy: When
 * the database is busy.
 */
drogon::Task<bool> KeeprBase::getLastLogoutTimeIfActiveCoro(
    std::optional<Types::ClientLoginData>& clientLoginData)
{
  try
  {
    const auto sessions = sessionsTable(
        clientLoginData->group.value());  // Resolve the safe table name.
    if (!sessions.has_value())            // Unknown group.
    {
      // Log it.
      Message::ErrorMessage("Unknown client group.");
      co_return false;  // No state loaded.
    }
    const std::string param_query = fmt::format(
        "WITH client_data AS (SELECT ses.id, ses.last_logout, "
        "client.active FROM {} ses "
        "LEFT JOIN {} client ON ses.id = client.id) "
        "SELECT id, last_logout, active FROM client_data WHERE id = $1;",
        sessions.value(),
        clientLoginData->group.value());  // Same parameterized query as the
                                          // synchronous version.
    pqxx::params params;                  // Parameter container.
    params.append(clientLoginData->clientId.value());  // Bind the client id.

    auto results =
        co_await databaseController()
            ->executeReadQueryCoro(param_query,
                                   params);  // Await the DB read without
                                             // blocking the thread.

    if (results.has_value())  // Got a result set.
    {
      if (!results->empty())  // Found a row.
      {
        clientLoginData->is_active =
            results.value().at("active").as_bool();  // Fill active flag.
        clientLoginData->lastLogoutTime =
            results.value().at("last_logout").as_string();  // Fill logout time.
        co_return true;                                     // State loaded.
      }
    }
    co_return false;  // No result or no row.
  }
  catch (const DatabaseBusy&)  // Database overloaded.
  {
    throw;  // Rethrow for the caller.
  }
  catch (std::exception& e)  // Other error.
  {
    CRITICALMESSAGE  // Log it.
  }
  co_return false;  // Failed to load state.
}

/**
 * Coroutine form of validateToken(), awaiting the session-state read first.
 * After the awaited read, applies the same logout/active/signature checks as
 * the synchronous version. The coroutine frame must outlive the awaited query.
 * clientLoginData: Identity/state accumulator.
 * message: Failure reason.
 * decodedToken: Token to verify.
 * Returns True if the token remains valid.
 * Throws DatabaseBusy: When the database is busy.
 */
drogon::Task<bool> KeeprBase::validateTokenCoro(
    std::optional<Types::ClientLoginData>&          clientLoginData,
    std::string&                                    message,
    jwt::decoded_jwt<jwt::traits::kazuho_picojson>& decodedToken)
{
  try
  {
    // Await refreshed session state before comparing the "llodt" claim.
    co_await getLastLogoutTimeIfActiveCoro(
        clientLoginData);  // Await the DB-backed state refresh.
    if (clientLoginData->lastLogoutTime !=
        decodedToken.get_payload_claim("llodt")
            .as_string())  // Stale token after a logout?
    {
      message =
          // Explain.
          "Client is logged out from server, Please login again.";
      co_return false;  // Reject.
    }

    if (!clientLoginData->is_active)  // Suspended account.
    {
      message =
          // Explain.
          "Account is suspend, please contact the administrator.";
      co_return false;  // Reject.
    }

    // Validate token claims
    auto verifier =
        createTokenVerifier(clientLoginData);  // Build the verifier.

    verifier.verify(decodedToken);  // Check signature and claims.

    // Token is valid
    co_return true;  // All checks passed.
  }
  catch (const jwt::error::token_verification_exception&
             e)  // Verification failed.
  {
    message =
        // Explain.
        fmt::format("Token verification failed:{}", e.what());
    Message::CriticalMessage(message);  // Log it.
    co_return false;                    // Reject.
  }
  catch (const DatabaseBusy&)  // Database overloaded.
  {
    throw;  // Rethrow.
  }
  catch (const std::exception& e)  // Other error.
  {
    message =
        // Explain.
        fmt::format("Error validating Token:{}", e.what());
    Message::CriticalMessage(message);  // Log it.
    co_return false;                    // Reject.
  }
  co_return false;  // Defensive fallback.
}

/**
 * Builds an HS256 verifier bound to the configured secret and the client's
 * claims. Verifying the issuer/type/subject/id plus the ip_address, group and
 * llodt claims ties a token to both the signing configuration and the specific
 * client session. clientLoginData: Must contain username, clientId, group,
 * ip_address and lastLogoutTime. Returns A verifier ready to validate a decoded
 * JWT. Throws std::runtime_error: If username, clientId or group is absent (the
 * explicit guard). Throws std::bad_optional_access: If ip_address or
 * lastLogoutTime is absent; those claims are read with `.value()` without a
 * presence check.
 */
jwt::verifier<jwt::default_clock, jwt::traits::kazuho_picojson>
KeeprBase::createTokenVerifier(
    const std::optional<Types::ClientLoginData>& clientLoginData) const
{
  if (!clientLoginData || !clientLoginData->username ||
      !clientLoginData->clientId ||
      !clientLoginData->group)  // Guard the claims that are explicitly checked.
  {
    throw std::runtime_error(
        "Invalid client login data");  // Missing essential identity: cannot
                                       // build a verifier.
  }

  return jwt::verify<
             jwt::traits::
                 kazuho_picojson>()  // Start a verifier using picojson traits.
      .allow_algorithm(jwt::algorithm::hs256{std::string(
          tokenManagerParameters_
              .secret)})  // Accept HS256 signed with the configured secret.
      .with_issuer(
          std::string(tokenManagerParameters_
                          .issuer))  // Require the configured issuer claim.
      .with_type(std::string(
          tokenManagerParameters_.type))  // Require the configured type claim.
      .with_subject(clientLoginData->username
                        .value())  // Require the username to match the subject.
      .with_id(std::to_string(
          clientLoginData->clientId
              .value()))  // Require the client id to match the token id.
      .with_claim("ip_address",
                  jwt::basic_claim<jwt::traits::kazuho_picojson>(
                      clientLoginData->ip_address
                          .value()))  // Require the IP claim to match.
      .with_claim("group",
                  jwt::basic_claim<jwt::traits::kazuho_picojson>(
                      clientLoginData->group
                          .value()))  // Require the group claim to match.
      .with_claim("llodt",
                  jwt::basic_claim<jwt::traits::kazuho_picojson>(
                      clientLoginData->lastLogoutTime
                          .value()));  // Require the logout-time claim to
                                       // match.
}

/**
 * Fetches the stored password hash for a username via DatabaseController.
 * username: User name to look up.
 * _group: Client group selecting the account table.
 * Returns JSON result from storage, or std::nullopt when not found.
 */
std::optional<jsoncons::json> api::v2::KeeprBase::getPasswordHashForUserName(
    const std::string& username, const std::string& _group)
{
  return databaseController()
      ->getPasswordHashForUserName(username, _group);  // Pure forwarder to the
                                                       // database controller.
}

/**
 * @brief Coroutine form of getPasswordHashForUserName().
 * @param[in] username User name to look up.
 * @param[in] _group Client group selecting the account table.
 * @return JSON result from storage.
 * @note Callers must drive the returned task; the frame must outlive the
 * awaited query.
 */
drogon::Task<std::optional<jsoncons::json>>
api::v2::KeeprBase::getPasswordHashForUserNameCoro(const std::string& username,
                                                   const std::string& _group)
{
  co_return co_await databaseController()
      ->getPasswordHashForUserNameCoro(username,
                                       _group);  // Await the DB controller and
                                                 // return its result.
}
