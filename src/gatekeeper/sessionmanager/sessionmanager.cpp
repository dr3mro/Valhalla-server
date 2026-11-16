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
#include "message/message.hpp"  // Message::CriticalMessage for logging.
#include "utils/global/global.hpp"
#include "utils/global/http.hpp"  // HTTP status codes and statusForException().

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
// Step 1 definition: fetch hash and check
// account state (signature continues next
// line).
// Credentials in; client data and the failure
// message are filled in through these references.
std::optional<std::string> SessionManager::fetchPasswordHash(
    const std::optional<Types::Credentials>& credentials,
    std::optional<Types::ClientLoginData>&   clientLoginData,
    std::string&                             message) {
  // Database and JSON operations can throw, so they run inside a guarded
  // block.
  try {
    auto client_object =
        getPasswordHashForUserName(credentials->username,
                                   clientLoginData->group
                                       .value());  // Look up the account row by
                                                   // username and client group
                                                   // (blocking read).

    // No row came back, so the username does not exist.
    if (!client_object.has_value() || client_object.value().empty()) {
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

    // Without an id there is no cache key to build.
    if (!clientLoginData->clientId.has_value()) {
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

    // A suspended account is not allowed to log in.
    if (!clientLoginData->is_active) {
      message += fmt::format(
          "username: [{}] is suspended, please contact the administrator",
          credentials->username);  // Tell the user their account is suspended.
      return std::nullopt;         // No hash to return; login cannot continue.
    }

    std::optional<std::string> password_hash =
        client_j.at("password").as_string();  // Read the stored password hash
                                              // as a string (may be absent).

    // Step 1 failed, so there is nothing to verify.
    if (!password_hash.has_value()) {
      message +=
          // Explain the missing hash.
          "Failed to find password hash from database, please try again";
      return std::nullopt;  // No hash to return; login cannot continue.
    }
    // Hand the hash back so step 2 can verify the
    // password.
    // A busy database is a special case handled by
    // the caller.
    return password_hash;
  } catch (const DatabaseBusy&) {
    // Re-throw unchanged so callers can retry or back off.
    // Any other error is reported through the message string.
    throw;
  } catch (const std::exception& e) {
    message += fmt::format("Error: {}",
                           e.what());  // Append the exception's text to the
                                       // failure message.
  }
  return std::nullopt;  // No hash to return; login cannot continue.
}

// Coroutine form of fetchPasswordHash(); see the header for the full contract.
// Coroutine step 1 definition
// (signature continues next line).
// Credentials in; client data and the failure
// message are filled in through these references.
drogon::Task<std::optional<std::string>> SessionManager::fetchPasswordHashCoro(
    const std::optional<Types::Credentials>& credentials,
    std::optional<Types::ClientLoginData>&   clientLoginData,
    std::string&                             message) {
  // Database and JSON operations can throw, so they run inside a guarded
  // block.
  try {
    auto client_object =
        co_await getPasswordHashForUserNameCoro(credentials->username,
                                                clientLoginData->group
                                                    .value());  // Await the
                                                                // same lookup
                                                                // without
                                                                // blocking an
                                                                // IO thread.

    // No row came back, so the username does not exist.
    if (!client_object.has_value() || client_object.value().empty()) {
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

    // Without an id there is no cache key to build.
    if (!clientLoginData->clientId.has_value()) {
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

    // A suspended account is not allowed to log in.
    if (!clientLoginData->is_active) {
      message += fmt::format(
          "username: [{}] is suspended, please contact the administrator",
          credentials->username);  // Tell the user their account is suspended.
      co_return std::nullopt;      // Coroutine return of no hash; login
                                   // cannot continue.
    }

    std::optional<std::string> password_hash =
        client_j.at("password").as_string();  // Read the stored password hash
                                              // as a string (may be absent).

    // Step 1 failed, so there is nothing to verify.
    if (!password_hash.has_value()) {
      message +=
          // Explain the missing hash.
          "Failed to find password hash from database, please try again";
      co_return std::nullopt;  // Coroutine return of no hash; login
                               // cannot continue.
    }
    // Coroutine return of the hash for step 2.
    // A busy database is a special case handled by
    // the caller.
    co_return password_hash;
  } catch (const DatabaseBusy&) {
    // Re-throw unchanged so callers can retry or back off.
    // Any other error is reported through the message string.
    throw;
  } catch (const std::exception& e) {
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
// Step 2 definition: verify then record
// login (signature continues next line).
// Rest of completeLogin's signature: the data
// accumulator and failure message.
bool SessionManager::completeLogin(
    const std::optional<Types::Credentials>& credentials,
    const std::string&                       password_hash,
    std::optional<Types::ClientLoginData>&   clientLoginData,
    std::string&                             message) {
  // Database and JSON operations can throw, so they run inside a guarded
  // block.
  try {
    // Check the password first: a failed login must not touch the session
    // row.
    // Check the password first; a
    // wrong password must not touch
    // the session row.
    if (!passwordCrypt->verifyPassword(credentials->password, password_hash)) {
      message +=
          // Deliberately vague so an attacker cannot learn which part was
          // wrong.
          "Invalid username/password, please try again";
      return false;  // Login failed.
    }

    // Record the login time and fetch the
    // previous logout time for the token.
    if (!setNowLoginTimeGetLastLogoutTime(clientLoginData)) {
      message +=  // Start building a message describing the failed time
                  // update.
          // First half of that message (string literals concatenate).
          "Failed to set now login and get last logout times , please "
          "try again";  // Second half of the message.
      // Login failed.
      // A busy database is a special case handled by
      // the caller.
      return false;
    }
  } catch (const DatabaseBusy&) {
    // Re-throw unchanged so callers can retry or back off.
    // Any other error is reported through the message string.
    throw;
  } catch (const std::exception& e) {
    message += fmt::format("Error: {}",
                           e.what());  // Append the exception's text to the
                                       // failure message.
    return false;                      // Login failed.
  }
  return true;  // Login succeeded.
}

/** @brief Runs fetchPasswordHash() then completeLogin() on the calling thread.
 */
// Runs both steps on the calling thread; true on success.
bool SessionManager::login(
    const std::optional<Types::Credentials>& credentials,
    std::optional<Types::ClientLoginData>&   clientLoginData,
    std::string&                             message) {
  const std::optional<std::string> password_hash =
      fetchPasswordHash(credentials,
                        clientLoginData,
                        message);  // Step 1: read the hash and validate the
                                   // account.
  // Step 1 failed, so there is nothing to verify.
  if (!password_hash.has_value()) {
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
// Ends the session and answers through the callback.
void SessionManager::logout(
    CALLBACK_&&                            callback,
    std::optional<Types::ClientLoginData>& clientLoginData) {
  // Database and JSON operations can throw, so they run inside a guarded
  // block.
  try {
    // Without a token there is nothing to log out.
    if (!clientLoginData.has_value() || !clientLoginData->token.has_value()) {
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
    // Database and JSON operations can throw, so they run inside a
    // guarded block.
    try {
      // Turn the token string into a structured
      // object (without verifying yet).
      // A malformed token cannot be decoded.
      decodedToken = jwt::decode<jwt::traits::kazuho_picojson>(
          clientLoginData->token.value());
    } catch (const std::exception&) {
      std::move(callback)(Http::Status::UNAUTHORIZED,
                          JsonHelper::stringify(JsonHelper::jsonify(
                              "Invalid token.")));  // Answer 401 because the
                                                    // token string is invalid.
      return;  // Stop here; no further work is meaningful.
    }

    // Extract and check the token's
    // identity claims.
    if (!decodeToken(clientLoginData, message, decodedToken.value())) {
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
        key);  // Look up any stored session for this client.
    // Only compare tokens when a session actually
    // exists.
    if (session.has_value()) {
      const std::string& stored_token = session->token.value_or(
          "");  // The token we stored at login (empty string if absent).
      const std::string& supplied_token = clientLoginData->token.value_or(
          "");  // The token the client sent with this request.
      // Empty or different-length strings
      // cannot match.
      if (stored_token.empty() || supplied_token.empty() ||
          stored_token.size() != supplied_token.size()) {
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
           // Walk every character of the tokens.
           ++i) {
        // Any differing character proves they
        // are not the same token.
        if (stored_token[i] != supplied_token[i]) {
          tokens_equal = false;  // Remember that a difference was found.
        }
      }
      // A mismatch means this token is not the one
      // stored for the session.
      if (!tokens_equal) {
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
    // Answer 200 with a success
    // message.
    // Any other error is reported through the message string.
    std::move(callback)(api::v2::Http::Status::OK,
                        api::v2::JsonHelper::stringify(
                            api::v2::JsonHelper::jsonify("Logout success.")));
  } catch (const std::exception& e) {
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
// Checks cached session against the token; true when
// they agree.
bool SessionManager::clientHasValidSession(
    std::optional<Types::ClientLoginData>& clientLoginData,
    std::string&                           message) {
  // Database and JSON operations can throw, so they run inside a guarded
  // block.
  try {
    auto decodedToken = jwt::decode<jwt::traits::kazuho_picojson>(
        clientLoginData->token
            .value());  // Parse the token string into a structured object.

    // Validate the token's claims and fill
    // clientLoginData.
    if (!decodeToken(clientLoginData, message, decodedToken)) {
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
        key);  // Fetch the cached session for this client.
    // Only compare tokens when a session actually
    // exists.
    if (session.has_value()) {
      // Compare the stored token with the supplied one in constant time
      // so a forged JWT that happens to carry a valid (group, client)
      // pair cannot hijack an existing session.
      const std::string& stored_token = session->token.value_or(
          "");  // The token we stored at login (empty string if absent).
      const std::string& supplied_token = clientLoginData->token.value_or(
          "");  // The token the client sent with this request.
      // Empty or different-length strings
      // cannot match.
      if (stored_token.empty() || supplied_token.empty() ||
          stored_token.size() != supplied_token.size()) {
        return false;  // Login failed.
      }
      bool tokens_equal = true;  // Assume a match, then prove otherwise
                                 // one character at a time.
      for (size_t i = 0; i < stored_token.size();
           // Walk every character of the tokens.
           ++i) {
        // Any differing character proves they
        // are not the same token.
        if (stored_token[i] != supplied_token[i]) {
          tokens_equal = false;  // Remember that a difference was found.
        }
      }
      // A mismatch means this token is not the one
      // stored for the session.
      if (!tokens_equal) {
        return false;  // Login failed.
      }
      // Trust the stored session's identity/expiry, not the (possibly
      // forged) JWT.
      clientLoginData =
          session.value();  // Trust the stored session's identity/expiry,
                            // not the possibly forged JWT.
      // Login succeeded.
      // A busy database is a special case handled by
      // the caller.
      return true;
    }
  } catch (const DatabaseBusy&) {
    // Re-throw unchanged so callers can retry or back off.
    // Any other error is reported through the message string.
    throw;
  } catch (const std::exception& e) {
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
// Insert the session into the cache with an expiry.
bool SessionManager::storeSession(
    std::optional<Types::ClientLoginData>& clientLoginData,
    std::string&                           message) {
  // Database and JSON operations can throw, so they run inside a guarded
  // block.
  try {
    // There is no session data to store.
    if (!clientLoginData.has_value()) {
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
    // Login succeeded.
    // A busy database is a special case handled by
    // the caller.
    return true;
  } catch (const DatabaseBusy&) {
    // Re-throw unchanged so callers can retry or back off.
    // Any other error is reported through the message string.
    throw;
  } catch (const std::exception& e) {
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
// Remove the cached session identified by group and
// id.
bool SessionManager::removeSession(
    std::optional<Types::ClientLoginData>& clientLoginData) {
  std::string
      message;  // Local message buffer (written but not returned on failure).
  // Database and JSON operations can throw, so they run inside a guarded
  // block.
  try {
    // There is no session data to store.
    if (!clientLoginData.has_value()) {
      message = "clientLoginData is empty";  // Explain why storage failed.
      return false;                          // Login failed.
    }
    // Without an id there is no cache key to build.
    if (!clientLoginData->clientId.has_value()) {
      message = "clientId has no value";  // Explain the missing id.
      return false;                       // Login failed.
    }
    // Without a group there is no
    // cache key to build.
    if (!clientLoginData->group.has_value()) {
      message = "group has no value";  // Explain the missing group.
      return false;                    // Login failed.
    }

    std::string key = fmt::format("{}_{}",
                                  clientLoginData->group.value(),
                                  clientLoginData->clientId
                                      .value());  // Build the cache key from
                                                  // group and client id.
    clientsSessionsList->remove(
        key);  // Delete the entry from the in-memory cache.
    // Login succeeded.
    // A busy database is a special case handled by
    // the caller.
    return true;
  } catch (const DatabaseBusy&) {
    // Re-throw unchanged so callers can retry or back off.
    // Any other error is reported through the message string.
    throw;
  } catch (const std::exception& e) {
    message = fmt::format("Error storing session:{}",
                          e.what());    // Build an error message for logging.
    Message::CriticalMessage(message);  // Log the error as critical.
  }
  return false;  // Login failed.
}
