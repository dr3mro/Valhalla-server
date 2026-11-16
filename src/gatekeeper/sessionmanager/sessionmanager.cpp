/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file sessionmanager.cpp
 * @brief The implementation of login, logout and session bookkeeping.
 *        (Originally: "Implementation for sessionmanager: Reads the stored
 * password hash after checking the account exists and is active.")
 * @details This file reads a client's stored hash, verifies the supplied
 *          password, records login/logout times, and manages the in-memory
 *          session cache. Tokens are decoded and compared to the stored session
 *          in constant time to stop forged tokens hijacking an account.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is where a username/password becomes a trusted session. GateKeeper calls
 * it for login and logout; the session data it stores is what later requests
 * rely on to know who is calling. Without it, no one could sign in or out.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - JWT (JSON Web Token): https://jwt.io/introduction
 * - Timing attacks / constant-time comparison:
 * https://en.wikipedia.org/wiki/Timing_attack
 * - C++ coroutines (co_await/co_return):
 * https://en.cppreference.com/w/cpp/language/coroutines
 * - HTTP 401 Unauthorized:
 * https://developer.mozilla.org/en-US/docs/Web/HTTP/Status/401
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// The declarations implemented here.
#include "gatekeeper/sessionmanager/sessionmanager.hpp"

#include <fmt/core.h>     // fmt::format for building messages.
#include <fmt/format.h>   // The full fmt formatting header.
#include <jwt-cpp/jwt.h>  // JWT decoding used when checking/logging out tokens.
// The JSON traits used by jwt-cpp.
#include <jwt-cpp/traits/kazuho-picojson/traits.h>

#include <chrono>     // Time/duration types for session expiry.
#include <cstdint>    // uint64_t for ids.
#include <ctime>      // C time helpers used by the token library.
#include <exception>  // std::exception for catch blocks.
// The JSON document returned by the database.
#include <jsoncons/basic_json.hpp>
#include <optional>  // std::optional for values that may be absent.
#include <string>    // std::string for keys and messages.
#include <utility>   // std::move for the callback.
// Helpers for building JSON responses.
#include <utils/jsonhelper/jsonhelper.hpp>

// DatabaseBusy, re-thrown so callers can retry.
#include "database/databasebusy.hpp"
#include "drogon/utils/coroutine.h"   // drogon::Task for the coroutine fetch.
#include "gatekeeper/types.hpp"       // Credentials and ClientLoginData.
#include "utils/global/callback.hpp"  // CALLBACK_, the HTTP response callback.
// Shared global helpers (e.g. CRITICALMESSAGE).
#include "utils/global/global.hpp"
#include "utils/global/http.hpp"  // HTTP status codes and statusForException().
#include "utils/message/message.hpp"  // Message::CriticalMessage for logging.

using SessionManager =
    api::v2::SessionManager;  // Short alias so definitions below can omit the
                              // namespace.

// Login is split in two so the slow part (password hashing) can run on the
// password worker:
//   fetchPasswordHash  - reads the stored hash and checks the account is active
//   (database reads only, no writes) completeLogin      - verifies the password
//   and only then records the login time
// login() runs both on the calling thread.
// GateKeeper calls the two steps itself when it wants to use the password pool;
// a failed password check therefore never writes a new login time to the
// database.
/**
 * Reads the stored password hash after checking the account exists and is
 * active. message: Receives the reason for a missing/suspended account or a
 * read error. Returns Stored hash, or `std::nullopt` if the account is
 * unusable. Throws DatabaseBusy: Propagated to the caller for retry/back-off.
 * See also: SessionManager::fetchPasswordHash
 */
std::optional<std::string>
SessionManager::fetchPasswordHash(  // Step 1 definition: fetch hash and check
                                    // account state (signature continues next
                                    // line).
    const std::optional<Types::Credentials>& credentials,
    std::optional<Types::ClientLoginData>&   clientLoginData,
    std::string& message)  // Credentials in; client data and the failure
                           // message are filled in through these references.
{
  try  // Database and JSON operations can throw, so they run inside a guarded
       // block.
  {
    auto client_object =
        getPasswordHashForUserName(credentials->username,
                                   clientLoginData->group
                                       .value());  // Look up the account row by
                                                   // username and client group
                                                   // (blocking read).

    if (!client_object.has_value() ||
        client_object.value()
            .empty())  // No row came back, so the username does not exist.
    {
      message =
          // Explain that there is no such account.
          "Failure: username does not exist, please try again.";
      return std::nullopt;  // No hash to return; login cannot continue.
    }

    jsoncons::json& client_j =
        client_object.value();  // Bind a reference to the returned account
                                // row for easy field access.

    clientLoginData->clientId =
        client_j.at("id").as<uint64_t>();  // Copy the numeric client id out of
                                           // the row into the caller's data.

    if (!clientLoginData->clientId
             .has_value())  // Without an id there is no cache key to build.
    {
      message +=
          // Append the reason for the failure.
          "Failed to find client id from database, please try again";
      return std::nullopt;  // No hash to return; login cannot continue.
    }

    clientLoginData->username =
        credentials->username;  // Remember which username was being checked.
    clientLoginData->is_active =
        client_j.at("active")
            .as<bool>();  // Copy the account's active/suspended flag.

    if (!clientLoginData
             ->is_active)  // A suspended account is not allowed to log in.
    {
      message += fmt::format(
          "username: [{}] is suspended, please contact the administrator",
          credentials->username);  // Tell the user their account is suspended.
      return std::nullopt;         // No hash to return; login cannot continue.
    }

    std::optional<std::string> password_hash =
        client_j.at("password").as_string();  // Read the stored password hash
                                              // as a string (may be absent).

    if (!password_hash
             .has_value())  // Step 1 failed, so there is nothing to verify.
    {
      message +=
          // Explain the missing hash.
          "Failed to find password hash from database, please try again";
      return std::nullopt;  // No hash to return; login cannot continue.
    }
    return password_hash;  // Hand the hash back so step 2 can verify the
                           // password.
  }
  catch (const DatabaseBusy&)  // A busy database is a special case handled by
                               // the caller.
  {
    throw;  // Re-throw unchanged so callers can retry or back off.
  }
  catch (const std::exception&
             e)  // Any other error is reported through the message string.
  {
    message += fmt::format("Error: {}",
                           e.what());  // Append the exception's text to the
                                       // failure message.
  }
  return std::nullopt;  // No hash to return; login cannot continue.
}

// Coroutine form of fetchPasswordHash(); see the header for the full contract.
drogon::Task<std::optional<std::string>>
SessionManager::fetchPasswordHashCoro(  // Coroutine step 1 definition
                                        // (signature continues next line).
    const std::optional<Types::Credentials>& credentials,
    std::optional<Types::ClientLoginData>&   clientLoginData,
    std::string& message)  // Credentials in; client data and the failure
                           // message are filled in through these references.
{
  try  // Database and JSON operations can throw, so they run inside a guarded
       // block.
  {
    auto client_object =
        co_await getPasswordHashForUserNameCoro(credentials->username,
                                                clientLoginData->group
                                                    .value());  // Await the
                                                                // same lookup
                                                                // without
                                                                // blocking an
                                                                // IO thread.

    if (!client_object.has_value() ||
        client_object.value()
            .empty())  // No row came back, so the username does not exist.
    {
      message =
          // Explain that there is no such account.
          "Failure: username does not exist, please try again.";
      co_return std::nullopt;  // Coroutine return of no hash; login
                               // cannot continue.
    }

    jsoncons::json& client_j =
        client_object.value();  // Bind a reference to the returned account
                                // row for easy field access.

    clientLoginData->clientId =
        client_j.at("id").as<uint64_t>();  // Copy the numeric client id out of
                                           // the row into the caller's data.

    if (!clientLoginData->clientId
             .has_value())  // Without an id there is no cache key to build.
    {
      message +=
          // Append the reason for the failure.
          "Failed to find client id from database, please try again";
      co_return std::nullopt;  // Coroutine return of no hash; login
                               // cannot continue.
    }

    clientLoginData->username =
        credentials->username;  // Remember which username was being checked.
    clientLoginData->is_active =
        client_j.at("active")
            .as<bool>();  // Copy the account's active/suspended flag.

    if (!clientLoginData
             ->is_active)  // A suspended account is not allowed to log in.
    {
      message += fmt::format(
          "username: [{}] is suspended, please contact the administrator",
          credentials->username);  // Tell the user their account is suspended.
      co_return std::nullopt;      // Coroutine return of no hash; login
                                   // cannot continue.
    }

    std::optional<std::string> password_hash =
        client_j.at("password").as_string();  // Read the stored password hash
                                              // as a string (may be absent).

    if (!password_hash
             .has_value())  // Step 1 failed, so there is nothing to verify.
    {
      message +=
          // Explain the missing hash.
          "Failed to find password hash from database, please try again";
      co_return std::nullopt;  // Coroutine return of no hash; login
                               // cannot continue.
    }
    co_return password_hash;  // Coroutine return of the hash for step 2.
  }
  catch (const DatabaseBusy&)  // A busy database is a special case handled by
                               // the caller.
  {
    throw;  // Re-throw unchanged so callers can retry or back off.
  }
  catch (const std::exception&
             e)  // Any other error is reported through the message string.
  {
    message += fmt::format("Error: {}",
                           e.what());  // Append the exception's text to the
                                       // failure message.
  }
  co_return std::nullopt;  // Coroutine return of no hash; login cannot
                           // continue.
}

/**
 * Verifies the password first; only a match updates the stored login time.
 * message: Receives the verification/update failure reason.
 * Returns True when verification and the login-time update both succeeded.
 * Throws DatabaseBusy: Propagated to the caller for retry/back-off.
 */
bool SessionManager::completeLogin(
    const std::optional<Types::Credentials>& credentials,
    const std::string& password_hash,  // Step 2 definition: verify then record
                                       // login (signature continues next line).
    std::optional<Types::ClientLoginData>& clientLoginData,
    std::string& message)  // Rest of completeLogin's signature: the data
                           // accumulator and failure message.
{
  try  // Database and JSON operations can throw, so they run inside a guarded
       // block.
  {
    // Check the password first: a failed login must not touch the session
    // row.
    if (!passwordCrypt
             ->verifyPassword(credentials->password,
                              password_hash))  // Check the password first; a
                                               // wrong password must not touch
                                               // the session row.
    {
      message +=
          // Deliberately vague so an attacker cannot learn which part was
          // wrong.
          "Invalid username/password, please try again";
      return false;  // Login failed.
    }

    if (!setNowLoginTimeGetLastLogoutTime(
            clientLoginData))  // Record the login time and fetch the
                               // previous logout time for the token.
    {
      message +=  // Start building a message describing the failed time
                  // update.
          // First half of that message (string literals concatenate).
          "Failed to set now login and get last logout times , please "
          "try again";  // Second half of the message.
      return false;     // Login failed.
    }
  }
  catch (const DatabaseBusy&)  // A busy database is a special case handled by
                               // the caller.
  {
    throw;  // Re-throw unchanged so callers can retry or back off.
  }
  catch (const std::exception&
             e)  // Any other error is reported through the message string.
  {
    message += fmt::format("Error: {}",
                           e.what());  // Append the exception's text to the
                                       // failure message.
    return false;                      // Login failed.
  }
  return true;  // Login succeeded.
}

/** @brief Runs fetchPasswordHash() then completeLogin() on the calling thread.
 */
bool SessionManager::login(
    const std::optional<Types::Credentials>& credentials,
    std::optional<Types::ClientLoginData>&   clientLoginData,
    std::string&
        message)  // Runs both steps on the calling thread; true on success.
{
  const std::optional<std::string> password_hash =
      fetchPasswordHash(credentials,
                        clientLoginData,
                        message);  // Step 1: read the hash and validate the
                                   // account.
  if (!password_hash
           .has_value())  // Step 1 failed, so there is nothing to verify.
  {
    return false;  // Login failed.
  }
  return completeLogin(credentials,
                       password_hash.value(),
                       clientLoginData,
                       message);  // Step 2: verify the password and record the
                                  // login.
}

/**
 * @brief Logs out the client and reports the result through @p callback.
 * @details Validates the token, checks it against the stored session in
 * constant time, records the logout time and removes the session. Every path
 * invokes the callback.
 */
void SessionManager::logout(
    CALLBACK_&& callback,
    std::optional<Types::ClientLoginData>&
        clientLoginData)  // Ends the session and answers through the callback.
{
  try  // Database and JSON operations can throw, so they run inside a guarded
       // block.
  {
    if (!clientLoginData.has_value() ||
        !clientLoginData->token
             .has_value())  // Without a token there is nothing to log out.
    {
      std::move(callback)(Http::Status::UNAUTHORIZED,
                          JsonHelper::stringify(JsonHelper::jsonify(
                              "Missing token.")));  // Answer 401 with a JSON
                                                    // body; the callback is
                                                    // moved exactly once.
      return;  // Stop here; no further work is meaningful.
    }

    std::optional<jwt::decoded_jwt<jwt::traits::kazuho_picojson>>
        decodedToken;     // Will hold the parsed token if decoding succeeds.
    std::string message;  // Local message buffer (written but not returned
                          // on failure).
    try  // Database and JSON operations can throw, so they run inside a
         // guarded block.
    {
      decodedToken = jwt::decode<jwt::traits::kazuho_picojson>(
          clientLoginData->token
              .value());  // Turn the token string into a structured
                          // object (without verifying yet).
    }
    catch (const std::exception&)  // A malformed token cannot be decoded.
    {
      std::move(callback)(Http::Status::UNAUTHORIZED,
                          JsonHelper::stringify(JsonHelper::jsonify(
                              "Invalid token.")));  // Answer 401 because the
                                                    // token string is invalid.
      return;  // Stop here; no further work is meaningful.
    }

    if (!decodeToken(clientLoginData,
                     message,
                     decodedToken.value()))  // Extract and check the token's
                                             // identity claims.
    {
      std::move(
          callback)(Http::Status::UNAUTHORIZED,
                    JsonHelper::stringify(JsonHelper::jsonify(
                        message)));  // Answer 401 with the specific reason.
      return;  // Stop here; no further work is meaningful.
    }

    // If a session is stored for this (group, client), the supplied token
    // must match it (constant time) to prevent a forged JWT from logging a
    // victim out.
    std::string key = fmt::format("{}_{}",
                                  clientLoginData->group.value(),
                                  clientLoginData->clientId
                                      .value());  // Build the cache key from
                                                  // group and client id.
    auto        session = clientsSessionsList->get(
        key);                 // Look up any stored session for this client.
    if (session.has_value())  // Only compare tokens when a session actually
                              // exists.
    {
      const std::string& stored_token = session->token.value_or(
          "");  // The token we stored at login (empty string if absent).
      const std::string& supplied_token = clientLoginData->token.value_or(
          "");  // The token the client sent with this request.
      if (stored_token.empty() || supplied_token.empty() ||
          stored_token.size() !=
              supplied_token.size())  // Empty or different-length strings
                                      // cannot match.
      {
        std::move(
            callback)(Http::Status::UNAUTHORIZED,
                      JsonHelper::stringify(JsonHelper::jsonify(
                          "Invalid token.")));  // Answer 401 because the token
                                                // string is invalid.
        return;  // Stop here; no further work is meaningful.
      }
      bool tokens_equal = true;  // Assume a match, then prove otherwise
                                 // one character at a time.
      for (size_t i = 0; i < stored_token.size();
           ++i)  // Walk every character of the tokens.
      {
        if (stored_token[i] !=
            supplied_token[i])  // Any differing character proves they
                                // are not the same token.
        {
          tokens_equal = false;  // Remember that a difference was found.
        }
      }
      if (!tokens_equal)  // A mismatch means this token is not the one
                          // stored for the session.
      {
        std::move(
            callback)(Http::Status::UNAUTHORIZED,
                      JsonHelper::stringify(JsonHelper::jsonify(
                          "Invalid token.")));  // Answer 401 because the token
                                                // string is invalid.
        return;  // Stop here; no further work is meaningful.
      }
    }

    setNowLogoutTime(clientLoginData->clientId.value(),
                     clientLoginData->group
                         .value());  // Record the logout time in the database.
    removeSession(
        clientLoginData);  // Delete the session from the in-memory cache.
    std::move(
        callback)(api::v2::Http::Status::OK,
                  api::v2::JsonHelper::stringify(api::v2::JsonHelper::jsonify(
                      "Logout success.")));  // Answer 200 with a success
                                             // message.
  }
  catch (const std::exception&
             e)  // Any other error is reported through the message string.
  {
    std::move(callback)(api::v2::Http::statusForException(e),
                        e.what());  // Answer with a status derived from the
                                    // exception and its message.
    CRITICALMESSAGE  // Log a critical error so operators notice the
                     // failure.
  }
}

/**
 * Validates a cached session against the supplied token and adopts the stored
 * identity. message: Receives the validation error reason. Returns True when a
 * matching session exists and the token is valid. Throws DatabaseBusy:
 * Propagated to the caller for retry/back-off.
 */
bool SessionManager::clientHasValidSession(
    std::optional<Types::ClientLoginData>& clientLoginData,
    std::string& message)  // Checks cached session against the token; true when
                           // they agree.
{
  try  // Database and JSON operations can throw, so they run inside a guarded
       // block.
  {
    auto decodedToken = jwt::decode<jwt::traits::kazuho_picojson>(
        clientLoginData->token
            .value());  // Parse the token string into a structured object.

    if (!decodeToken(clientLoginData,
                     message,
                     decodedToken))  // Validate the token's claims and fill
                                     // clientLoginData.
    {
      message =
          fmt::format("Token is not valid: {}",
                      message);  // Prefix the underlying reason with context.
      return false;              // Login failed.
    }

    std::string key = fmt::format("{}_{}",
                                  clientLoginData->group.value(),
                                  clientLoginData->clientId
                                      .value());  // Build the cache key from
                                                  // group and client id.

    auto session = clientsSessionsList->get(
        key);                 // Fetch the cached session for this client.
    if (session.has_value())  // Only compare tokens when a session actually
                              // exists.
    {
      // Compare the stored token with the supplied one in constant time
      // so a forged JWT that happens to carry a valid (group, client)
      // pair cannot hijack an existing session.
      const std::string& stored_token = session->token.value_or(
          "");  // The token we stored at login (empty string if absent).
      const std::string& supplied_token = clientLoginData->token.value_or(
          "");  // The token the client sent with this request.
      if (stored_token.empty() || supplied_token.empty() ||
          stored_token.size() !=
              supplied_token.size())  // Empty or different-length strings
                                      // cannot match.
      {
        return false;  // Login failed.
      }
      bool tokens_equal = true;  // Assume a match, then prove otherwise
                                 // one character at a time.
      for (size_t i = 0; i < stored_token.size();
           ++i)  // Walk every character of the tokens.
      {
        if (stored_token[i] !=
            supplied_token[i])  // Any differing character proves they
                                // are not the same token.
        {
          tokens_equal = false;  // Remember that a difference was found.
        }
      }
      if (!tokens_equal)  // A mismatch means this token is not the one
                          // stored for the session.
      {
        return false;  // Login failed.
      }
      // Trust the stored session's identity/expiry, not the (possibly
      // forged) JWT.
      clientLoginData =
          session.value();  // Trust the stored session's identity/expiry,
                            // not the possibly forged JWT.
      return true;          // Login succeeded.
    }
  }
  catch (const DatabaseBusy&)  // A busy database is a special case handled by
                               // the caller.
  {
    throw;  // Re-throw unchanged so callers can retry or back off.
  }
  catch (const std::exception&
             e)  // Any other error is reported through the message string.
  {
    message = fmt::format("Error storing session:{}",
                          e.what());    // Build an error message for logging.
    Message::CriticalMessage(message);  // Log the error as critical.
  }
  return false;  // Login failed.
}

/**
 * Caches the session under `"<group>_<id>"`, using `expireTime` as the MemCache
 * TTL. `expireTime` is the JWT `exp` claim as a system_clock duration since the
 * epoch (populated by decodeToken), so the value handed to MemCache::insert is
 * that absolute duration, not a remaining-lifetime interval. message: Receives
 * the failure reason. Returns True when the session was inserted. Throws
 * DatabaseBusy: Propagated to the caller for retry/back-off.
 */
bool SessionManager::storeSession(
    std::optional<Types::ClientLoginData>& clientLoginData,
    std::string& message)  // Insert the session into the cache with an expiry.
{
  try  // Database and JSON operations can throw, so they run inside a guarded
       // block.
  {
    if (!clientLoginData.has_value())  // There is no session data to store.
    {
      message = "clientLoginData is empty";  // Explain why storage failed.
      return false;                          // Login failed.
    }
    std::string key = fmt::format("{}_{}",
                                  clientLoginData->group.value(),
                                  clientLoginData->clientId
                                      .value());  // Build the cache key from
                                                  // group and client id.
    clientsSessionsList
        ->insert(key,
                 clientLoginData.value(),
                 std::chrono::duration_cast<std::chrono::seconds>(
                     clientLoginData
                         ->expireTime));  // Store the session under the key
                                          // with a TTL (see the header note
                                          // about expireTime).
    return true;                          // Login succeeded.
  }
  catch (const DatabaseBusy&)  // A busy database is a special case handled by
                               // the caller.
  {
    throw;  // Re-throw unchanged so callers can retry or back off.
  }
  catch (const std::exception&
             e)  // Any other error is reported through the message string.
  {
    message = fmt::format("Error storing session:{}",
                          e.what());    // Build an error message for logging.
    Message::CriticalMessage(message);  // Log the error as critical.
  }
  return false;  // Login failed.
}

/**
 * @brief Removes the cached session identified by the client's group and id.
 * @return True when a session key could be built and the removal was attempted.
 * @throws DatabaseBusy Propagated to the caller for retry/back-off.
 */
bool SessionManager::removeSession(
    std::optional<Types::ClientLoginData>&
        clientLoginData)  // Remove the cached session identified by group and
                          // id.
{
  std::string
      message;  // Local message buffer (written but not returned on failure).
  try  // Database and JSON operations can throw, so they run inside a guarded
       // block.
  {
    if (!clientLoginData.has_value())  // There is no session data to store.
    {
      message = "clientLoginData is empty";  // Explain why storage failed.
      return false;                          // Login failed.
    }
    if (!clientLoginData->clientId
             .has_value())  // Without an id there is no cache key to build.
    {
      message = "clientId has no value";  // Explain the missing id.
      return false;                       // Login failed.
    }
    if (!clientLoginData->group.has_value())  // Without a group there is no
                                              // cache key to build.
    {
      message = "group has no value";  // Explain the missing group.
      return false;                    // Login failed.
    }

    std::string key = fmt::format("{}_{}",
                                  clientLoginData->group.value(),
                                  clientLoginData->clientId
                                      .value());  // Build the cache key from
                                                  // group and client id.
    clientsSessionsList->remove(
        key);     // Delete the entry from the in-memory cache.
    return true;  // Login succeeded.
  }
  catch (const DatabaseBusy&)  // A busy database is a special case handled by
                               // the caller.
  {
    throw;  // Re-throw unchanged so callers can retry or back off.
  }
  catch (const std::exception&
             e)  // Any other error is reported through the message string.
  {
    message = fmt::format("Error storing session:{}",
                          e.what());    // Build an error message for logging.
    Message::CriticalMessage(message);  // Log the error as critical.
  }
  return false;  // Login failed.
}
