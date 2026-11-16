/**
 * @file gatekeeper.cpp
 * @brief Implementation for gatekeeper.
 * @details This file turns the GateKeeper declarations into working code. Login
 * parses the request, looks up the stored password hash, then moves the slow
 * hashing, the session write and the token creation onto a password-worker
 * thread so the server's IO threads stay free. Authentication first tries a
 * cached session and only falls back to verifying the JWT when there is no live
 * session. All permission helpers are one-line forwarders to PermissionManager,
 * and a set of explicit template instantiations generates those helpers for
 * every supported entity type.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is where the security facade actually does its job. Controllers call
 * login(), logout() and the can*() helpers; filters call isDosAttack(); the
 * authentication filter calls isAuthenticationValid(). This file wires those
 * calls to SessionManager, TokenManager, DOSDetector, PermissionManager and
 * PasswordWorker. If it were missing, the declared GateKeeper interface would
 * have no implementation and nothing security-related would link or run. The
 * macro block at the bottom is what makes the permission helpers exist for
 * concrete types like User, Patient and ClinicAppointment.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Callback style (call this function when the answer is ready):
 * https://en.cppreference.com/w/cpp/utility/functional/function
 * - Lambda capture, especially capturing shared_ptrs by value:
 * https://en.cppreference.com/w/cpp/language/lambda
 * - Thread pool / offloading slow work:
 * https://en.wikipedia.org/wiki/Thread_pool
 * - std::move (transfer ownership instead of copying):
 * https://en.cppreference.com/w/cpp/utility/move
 * - Coroutine / co_await: https://en.cppreference.com/w/cpp/language/coroutines
 * - Macro / preprocessor directive:
 * https://en.cppreference.com/w/cpp/preprocessor
 * - Explicit template instantiation:
 * https://en.cppreference.com/w/cpp/language/templates
 * - JWT: https://jwt.io/introduction
 * - HTTP status codes: https://developer.mozilla.org/en-US/docs/Web/HTTP/Status
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// The interface this file implements; brings in all the collaborator types.
#include "gatekeeper.hpp"

// fmt::format is used to build diagnostic messages on parse failures.
#include <fmt/core.h>
// Additional fmt formatting support used by the message macros.
#include <fmt/format.h>

#include <cstdint>    // uint64_t used by the permission helpers.
#include <exception>  // std::exception, caught by all the try/catch blocks.
// jsoncons::json used to build the success payload.
#include <jsoncons/basic_json.hpp>
// std::optional used for credentials, hashes and session data.
#include <optional>
#include <string>       // std::string used for messages and response bodies.
#include <string_view>  // std::string_view parameters for request data.
#include <utility>      // std::move used to hand data to the worker lambda.

#include "drogon/utils/coroutine.h"  // drogon::Task coroutine support.
// Request / Status used by isDosAttack.
#include "gatekeeper/dosdetector/dosdetector.hpp"
// IWYU pragma: keep  // Makes every entity type complete for the template
// instantiations at the bottom.
#include "gatekeeper/includes.hpp"
// PasswordWorker and runOnPasswordWorker.
#include "gatekeeper/passwordcrypt/passwordworker.hpp"
// PermissionManager::can* methods forwarded below.
#include "gatekeeper/permissionmanager/permissionmanager.hpp"
// SessionManager: hash lookup, login completion, sessions.
#include "gatekeeper/sessionmanager/sessionmanager.hpp"
// TokenManager: generateToken / isTokenValid.
#include "gatekeeper/tokenmanager/tokenmanager.hpp"
#include "gatekeeper/types.hpp"  // Credentials and ClientLoginData types.
#include "store/store.hpp"       // Store::getObject, used by the constructor.
#include "utils/global/callback.hpp"  // CALLBACK_ type alias.
// Client_t concept for the client-only helpers.
#include "utils/global/concepts.hpp"
// CRITICALMESSAGERESPONSE error-reply macro.
#include "utils/global/global.hpp"
#include "utils/global/http.hpp"  // Http::Status / Http::Error.
// Requester type passed to permission checks.
#include "utils/global/requester.hpp"
using GateKeeper =
    api::v2::GateKeeper;  // Short alias so the definitions below read cleanly.
using Credentials = api::v2::Types::Credentials;  // Short alias for the parsed
                                                  // login credentials.
using ClientLoginData =
    api::v2::Types::ClientLoginData;  // Short alias for the identity
                                      // accumulator.
using PermissionManager =
    api::v2::PermissionManager;  // Short alias for the permission collaborator.
using HttpStatus = api::v2::Http::Status;  // Short alias for HTTP status codes.

// All collaborators are process-wide singletons from Store; GateKeeper only
// holds shared handles.
GateKeeper::GateKeeper()
    : sessionManager_(
          Store::getObject<SessionManager>()),  // Fetch (or lazily create) the
                                                // shared session manager.
      tokenManager_(
          Store::getObject<api::v2::TokenManager>()),  // Fetch the shared JWT
                                                       // manager.
      dosDetector_(
          Store::getObject<DOSDetector>()),  // Fetch the shared abuse detector.
      permissionManager_(
          Store::getObject<api::v2::PermissionManager>()),  // Fetch the shared
                                                            // permission
                                                            // manager.
      passwordWorker_(
          Store::getObject<PasswordWorker>())  // Fetch the shared password
                                               // worker pool.
{
}
void GateKeeper::login(CALLBACK_&&        callback,
                       std::string_view   data,
                       const std::string& ip_address,
                       std::string_view   group)
{
  try
  {
    bool success = false;  // Set true only if JSON and both fields parse;
                           // starts false ("not proven").
    std::string
        message;  // Accumulates a human-readable reason if anything fails.
    std::optional<Types::Credentials> credentials =
        parse_credentials(data, message, success);  // Parse username/password
                                                    // without throwing.

    // Malformed JSON or missing fields fail fast before any database or
    // worker work.
    if (!success ||
        !credentials.has_value())  // Either the parse failed or no credentials
                                   // came back: reject immediately.
    {
      std::move(
          callback)(HttpStatus::BAD_REQUEST,
                    "credentials parse error : " +
                        message);  // Tell the client its body was bad (400).
      return;                      // Stop; nothing else to do.
    }

    std::optional<Types::ClientLoginData> clientLoginData =
        Types::ClientLoginData{};  // Fresh identity bag, value-initialized
                                   // so bools start false.
    clientLoginData->group =
        group;  // Record which group (selects the session table later).
    clientLoginData->ip_address =
        ip_address;  // Record the IP so it can be bound into the token.

    // Database reads only, on this thread. Everything slow (password
    // hashing) and everything that writes happens on the worker.
    std::optional<std::string> password_hash =
        sessionManager_
            ->fetchPasswordHash(credentials,
                                clientLoginData,
                                message);  // Read the stored hash; nullopt
                                           // means unknown account.
    // A missing hash means the account is unknown or unusable; report
    // UNAUTHORIZED without hinting which.
    if (!password_hash.has_value())  // No hash -> do not continue the login.
    {
      std::move(callback)(Http::Status::UNAUTHORIZED,
                          "Login failure: " +
                              message);  // Deliberately vague so attackers
                                         // cannot enumerate users.
      return;
    }

    // Lambda captures shared_ptrs by value and all mutable data by move:
    // the worker outlives this call frame, and callback is invoked on the
    // worker thread.
    runOnPasswordWorker(
        *passwordWorker_,
        std::move(callback),  // Queue the slow work; if the queue is full
                              // the callback gets a 503.
        [sessionManager  = sessionManager_,
         tokenManager    = tokenManager_,
         credentials     = std::move(credentials),
         password_hash   = std::move(password_hash).value(),
         clientLoginData = std::move(clientLoginData),
         message         = std::move(message)](CALLBACK_&& callback) mutable
        {
          try
          {
            // Password comparison plus session write happen together on
            // the worker; failure here is deliberately
            // indistinguishable from a bad password.
            if (!sessionManager->completeLogin(credentials,
                                               password_hash,
                                               clientLoginData,
                                               message) ||
                !clientLoginData.has_value())  // Wrong password OR no
                                               // session data -> fail.
            {
              callback(Http::Status::UNAUTHORIZED,
                       "Login failure: " + message);  // Same vague error as a
                                                      // missing account.
              return;
            }

            // Token creation depends on the fully populated
            // clientLoginData above.
            if (!tokenManager->generateToken(
                    clientLoginData))  // Build and sign the JWT from
                                       // the identity fields.
            {
              callback(Http::Status::INTERNAL_SERVER_ERROR,
                       "failed to generate token");  // Internal problem
                                                     // signing the token
                                                     // (500).
              return;
            }

            // Success payload echoes the identity back so the client
            // can cache it.
            jsoncons::json token_object;  // Build the JSON object that
                                          // will be the response body.
            token_object["token"] =
                clientLoginData->token;  // The signed JWT the client
                                         // must send on later requests.
            token_object["username"] =
                clientLoginData->username;  // Echo the user name.
            token_object["client_id"] =
                clientLoginData->clientId;  // Echo the numeric id.
            token_object["group"] = clientLoginData->group;  // Echo the group.
            token_object["ipAddress"] =
                clientLoginData->ip_address;  // Echo the bound IP.

            callback(Http::Status::OK,
                     token_object
                         .as<std::string>());  // Reply 200 with the JSON
                                               // serialized to a string.
          }
          catch (const std::exception&
                     e)  // Any unexpected error inside the worker task.
          {
            CRITICALMESSAGERESPONSE  // Log it and answer through
                                     // callback with the right HTTP
                                     // status.
          }
        });
  }
  catch (const std::exception&
             e)  // Any unexpected error during parsing or hash lookup.
  {
    CRITICALMESSAGERESPONSE  // Log and reply; the callback is guaranteed to
                             // be called exactly once.
  }
}

drogon::Task<void> GateKeeper::loginAsync(CALLBACK_&&        callback,
                                          std::string_view   data,
                                          const std::string& ip_address,
                                          std::string_view   group)
{
  try
  {
    bool success = false;  // Tracks whether JSON and required fields parsed.
    std::string                       message;  // Collects failure details.
    std::optional<Types::Credentials> credentials =
        parse_credentials(data, message, success);  // Same parsing as login().

    if (!success || !credentials.has_value())  // Bad body: fail before any
                                               // awaited or worker work.
    {
      std::move(callback)(HttpStatus::BAD_REQUEST,
                          "credentials parse error : " +
                              message);  // Reply 400 with the parse error.
      co_return;  // Leave the coroutine (nothing more to do).
    }

    std::optional<Types::ClientLoginData> clientLoginData =
        Types::ClientLoginData{};         // Fresh identity accumulator.
    clientLoginData->group      = group;  // Group selects the session table.
    clientLoginData->ip_address = ip_address;  // IP is bound into the token.

    // Runs on a Drogon executor: the hash lookup yields cooperatively
    // instead of blocking the IO thread; the coroutine frame stays alive
    // for the await duration.
    std::optional<std::string> password_hash =
        co_await sessionManager_
            ->fetchPasswordHashCoro(credentials,
                                    clientLoginData,
                                    message);  // Await the DB read; nullopt
                                               // means no account.
    if (!password_hash.has_value())            // Unknown/unusable account.
    {
      std::move(callback)(Http::Status::UNAUTHORIZED,
                          "Login failure: " + message);  // Vague 401 reply.
      co_return;
    }

    runOnPasswordWorker(*passwordWorker_,
                        std::move(callback),  // Hand slow verification + writes
                                              // to the worker pool.
                        [sessionManager  = sessionManager_,
                         tokenManager    = tokenManager_,
                         credentials     = std::move(credentials),
                         password_hash   = std::move(password_hash).value(),
                         clientLoginData = std::move(clientLoginData),
                         message =
                             std::move(message)](CALLBACK_&& callback) mutable
                        {
                          try
                          {
                            // Password comparison plus session write happen
                            // together on the worker; failure here is
                            // deliberately indistinguishable from a bad
                            // password.
                            if (!sessionManager->completeLogin(credentials,
                                                               password_hash,
                                                               clientLoginData,
                                                               message) ||
                                !clientLoginData
                                     .has_value())  // Bad password or
                                                    // missing session data.
                            {
                              callback(Http::Status::UNAUTHORIZED,
                                       "Login failure: " +
                                           message);  // Vague 401 reply.
                              return;
                            }

                            // Token creation depends on the fully populated
                            // clientLoginData above.
                            if (!tokenManager->generateToken(
                                    clientLoginData))  // Sign the JWT from the
                                                       // identity fields.
                            {
                              callback(Http::Status::INTERNAL_SERVER_ERROR,
                                       "failed to generate token");  // Signing
                                                                     // failed
                                                                     // (500).
                              return;
                            }

                            // Success payload echoes the identity back so the
                            // client can cache it.
                            jsoncons::json token_object;  // Response body under
                                                          // construction.
                            token_object["token"] =
                                clientLoginData->token;  // Signed JWT.
                            token_object["username"] =
                                clientLoginData->username;  // User name.
                            token_object["client_id"] =
                                clientLoginData->clientId;  // Numeric id.
                            // Group.
                            token_object["group"] = clientLoginData->group;
                            token_object["ipAddress"] =
                                clientLoginData->ip_address;  // Bound IP.

                            callback(Http::Status::OK,
                                     token_object
                                         .as<std::string>());  // Reply 200 with
                                                               // JSON text.
                          }
                          catch (const std::exception&
                                     e)  // Unexpected worker error.
                          {
                            CRITICALMESSAGERESPONSE  // Log and answer through
                                                     // callback.
                          }
                        });
  }
  catch (const std::exception& e)  // Unexpected error before/around the await.
  {
    CRITICALMESSAGERESPONSE  // Log and reply.
  }
  co_return;  // Coroutine form always exits with co_return (even after a
              // normal dispatch).
}

void GateKeeper::logout(CALLBACK_&&                       callback,
                        const std::optional<std::string>& token,
                        const std::string&                ip_address,
                        const std::string&                group)
{
  try
  {
    // Only the fields needed by SessionManager::logout are populated here.
    std::optional<ClientLoginData> clientLoginData =
        ClientLoginData{};  // Minimal identity bag for the logout helper.
    clientLoginData->token = token;  // Carry the token for downstream
                                     // handling (not validated here).
    clientLoginData->group = group;  // Group selects the session table.
    clientLoginData->ip_address =
        ip_address;  // IP recorded in the logout data.
    sessionManager_
        ->logout(std::move(callback),
                 clientLoginData);  // Delegate the actual logout/write, passing
                                    // the reply callback along.
  }
  catch (const std::exception&
             e)  // Unexpected error while preparing or delegating.
  {
    CRITICALMESSAGERESPONSE  // Log and reply with an appropriate HTTP
                             // status.
  }
}

bool GateKeeper::isAuthenticationValid(
    std::optional<Types::ClientLoginData>& clientLoginData,
    std::string&                           message)
{
  // Fast path: a live session avoids re-verifying the JWT signature.
  if (sessionManager_->clientHasValidSession(clientLoginData,
                                             message))  // Is there a cached,
                                                        // still-valid session?
  {
    return true;  // Yes: authenticated, no need to check the token.
  }

  // Cold path: validate the token, then rehydrate the session cache from its
  // claims.
  if (tokenManager_->isTokenValid(clientLoginData,
                                  message))  // No session, so check the JWT
                                             // signature and claims.
  {
    return sessionManager_
        ->storeSession(clientLoginData,
                       message);  // Token ok: rebuild the session cache and
                                  // return that result.
  }
  return false;  // Neither session nor token proved the client: reject.
}

drogon::Task<bool> GateKeeper::isAuthenticationValidAsync(
    std::optional<Types::ClientLoginData>& clientLoginData,
    std::string&                           message)
{
  if (sessionManager_->clientHasValidSession(clientLoginData,
                                             message))  // Cached session still
                                                        // valid? (synchronous)
  {
    co_return true;  // Fast path success.
  }

  // Only the token verification is awaited; both session calls remain
  // synchronous.
  if (co_await tokenManager_
          ->isTokenValidCoro(clientLoginData,
                             message))  // Await the token check without
                                        // blocking a thread.
  {
    co_return sessionManager_
        ->storeSession(clientLoginData,
                       message);  // Rebuild the session cache and return that
                                  // result.
  }
  co_return false;  // Not authenticated.
}

void GateKeeper::removeSession(std::optional<uint64_t> client_id,
                               const std::string&      group)
{
  std::optional<ClientLoginData> clientLoginData =
      ClientLoginData{};  // Minimal bag for the remove helper.
  clientLoginData->clientId =
      client_id;  // Which client's session to drop (optional id).
  clientLoginData->group = group;  // Group selects the session table.
  sessionManager_->removeSession(
      clientLoginData);  // Delegate the actual removal.
}

DOSDetector::Status GateKeeper::isDosAttack(const DOSDetector::Request& request)
{
  return dosDetector_->is_dos_attack(request);
}  // One-line forwarder; the filter interprets the verdict.

std::optional<jsoncons::json> GateKeeper::
    parse_data(/* NOLINT(readability-convert-member-functions-to-static)*/
               std::string_view data,
               std::string&     message,
               bool&            success)
{
  success = false;          // Assume failure until parsing actually succeeds.
  jsoncons::json object_j;  // Will hold the parsed JSON document.
  try
  {
    object_j = jsoncons::json::parse(
        data);  // Turn the raw body text into a JSON object.
  }
  catch (const std::exception& e)
  {
    // Accumulate diagnostics into message rather than throwing across the
    // callback boundary.
    message = fmt::format("Json parsing error : {} - {}",
                          message,
                          e.what());  // Append the parse error to any existing
                                      // message.
    return std::nullopt;              // Signal failure to the caller.
  }
  success = true;   // Parsing worked.
  return object_j;  // Hand back the parsed document.
}

std::optional<Credentials> GateKeeper::parse_credentials(std::string_view data,
                                                         std::string& message,
                                                         bool&        success)
{
  success = false;  // Assume failure until both fields are extracted.

  std::optional<jsoncons::json> credentials_j =
      parse_data(data, message, success);  // Step 1: parse the JSON.
  if (!success ||
      !credentials_j
           .has_value())  // If parsing failed there is nothing to read.
  {
    return std::nullopt;  // Propagate failure.
  }

  Types::Credentials credentials;  // Result object, filled in below.
  try
  {
    // at() throws for a missing key; as<std::string>() throws for the wrong
    // type.
    credentials.username =
        credentials_j.value()
            .at("username")
            .as<std::string>();  // Read the "username" member as text.
    credentials.password =
        credentials_j.value()
            .at("password")
            .as<std::string>();  // Read the "password" member as text.
  }
  catch (const std::exception& e)
  {
    message = fmt::format("Credentials parsing error : {} - {}",
                          message,
                          e.what());  // Append why extraction failed.
    return std::nullopt;              // Missing or ill-typed field -> failure.
  }
  success = true;      // Both fields were present and were strings.
  return credentials;  // Hand back the credentials.
}

// The permission checks are pure forwarders: all policy lives in
// PermissionManager. See the header for the per-method contracts and template
// parameter documentation. Each function below simply passes its arguments to
// the matching PermissionManager method, so GateKeeper adds no security rules
// of its own - changing a rule means changing the manager.
template <typename T>
bool GateKeeper::canCreate(const Requester&                     requester,
                           const std::optional<jsoncons::json>& data_j,
                           Http::Error&                         error)
{
  return permissionManager_
      ->canCreate<T>(requester,
                     data_j,
                     error);  // Ask the manager; it fills error on denial.
}

template <typename T>
bool GateKeeper::canRead(const Requester& requester,
                         const uint64_t   entity_id,
                         Http::Error&     error)
{
  return permissionManager_
      ->canRead<T>(requester,
                   entity_id,
                   error);  // Forward read permission check.
}

template <typename T>
bool GateKeeper::canUpdate(const Requester& requester,
                           const uint64_t   entity_id,
                           Http::Error&     error)
{
  return permissionManager_
      ->canUpdate<T>(requester,
                     entity_id,
                     error);  // Forward update permission check.
}

template <typename T>
bool GateKeeper::canDelete(const Requester& requester,
                           const uint64_t   entity_id,
                           Http::Error&     error)
{
  return permissionManager_
      ->canDelete<T>(requester,
                     entity_id,
                     error);  // Forward delete permission check.
}

template <typename T>
bool GateKeeper::canManageStaff(const Requester& requester,
                                uint64_t         _id,
                                Http::Error&     error)
{
  return permissionManager_
      ->canManageStaff<T>(requester,
                          _id,
                          error);  // Forward staff-management check.
}

template <Client_t T>
bool GateKeeper::canToggleActive(const Requester& requester,
                                 const uint64_t   _id,
                                 Http::Error&     error)
{
  return permissionManager_->canToggleActive<
      T>(requester,
         _id,
         error);  // Forward active-flag toggle check (client types only).
}

template <Client_t T>
bool GateKeeper::canGetServices(const Requester& requester,
                                const uint64_t   _id,
                                Http::Error&     error)
{
  return permissionManager_->canGetServices<
      T>(requester,
         _id,
         error);  // Forward list-services check (client types only).
}

template <typename T>
drogon::Task<bool> GateKeeper::canCreateAsync(
    const Requester&                     requester,
    const std::optional<jsoncons::json>& data_j,
    Http::Error&                         error)
{
  co_return co_await permissionManager_
      ->canCreateAsync<T>(requester,
                          data_j,
                          error);  // Await the manager and return its verdict.
}

template <typename T>
drogon::Task<bool> GateKeeper::canReadAsync(const Requester& requester,
                                            const uint64_t   entity_id,
                                            Http::Error&     error)
{
  co_return co_await permissionManager_
      ->canReadAsync<T>(requester,
                        entity_id,
                        error);  // Await the manager and return its verdict.
}

template <typename T>
drogon::Task<bool> GateKeeper::canUpdateAsync(const Requester& requester,
                                              const uint64_t   entity_id,
                                              Http::Error&     error)
{
  co_return co_await permissionManager_
      ->canUpdateAsync<T>(requester,
                          entity_id,
                          error);  // Await the manager and return its verdict.
}

template <typename T>
drogon::Task<bool> GateKeeper::canDeleteAsync(const Requester& requester,
                                              const uint64_t   entity_id,
                                              Http::Error&     error)
{
  co_return co_await permissionManager_
      ->canDeleteAsync<T>(requester,
                          entity_id,
                          error);  // Await the manager and return its verdict.
}

template <typename T>
drogon::Task<bool> GateKeeper::canManageStaffAsync(const Requester& requester,
                                                   uint64_t         _id,
                                                   Http::Error&     error)
{
  co_return co_await permissionManager_->canManageStaffAsync<
      T>(requester, _id, error);  // Await the manager and return its verdict.
}

template <Client_t T>
drogon::Task<bool> GateKeeper::canToggleActiveAsync(const Requester& requester,
                                                    const uint64_t   _id,
                                                    Http::Error&     error)
{
  co_return co_await permissionManager_->canToggleActiveAsync<
      T>(requester, _id, error);  // Await the manager and return its verdict.
}

template <Client_t T>
drogon::Task<bool> GateKeeper::canGetServicesAsync(const Requester& requester,
                                                   const uint64_t   _id,
                                                   Http::Error&     error)
{
  co_return co_await permissionManager_->canGetServicesAsync<
      T>(requester, _id, error);  // Await the manager and return its verdict.
}

// Explicit instantiations: the templates are defined here but used by other
// translation units, so each supported entity type needs a definition in this
// TU. CLIENT adds client-only checks, ENTITY adds service-staff checks, CRUD
// adds the four basic operations; the coroutine variants mirror each
// synchronous form. Why: a template is only turned into real machine code when
// it is used. Because the controllers live in different .cpp files, this file
// must force the compiler to emit a version of each helper for every concrete
// type. Each macro below expands to several "template bool
// GateKeeper::canX<Type>..." lines. The lines inside a macro end with a
// backslash (line continuation), so comments are added above the macros rather
// than on those lines - a trailing comment after a backslash would break the
// macro. canCreate specializations
#define INSTANTIATE_GATEKEEPER_CRUD(TYPE) /* NOLINT  */                  \
  template bool                                                          \
  GateKeeper::canCreate<TYPE>(const Requester&,                          \
                              const std::optional<jsoncons::json>&,      \
                              Http::Error&);                             \
  template bool GateKeeper::canRead<TYPE>(const Requester&,              \
                                          const uint64_t entity_id,      \
                                          Http::Error&);                 \
  template bool GateKeeper::canUpdate<TYPE>(const Requester&,            \
                                            const uint64_t entity_id,    \
                                            Http::Error&);               \
  template bool GateKeeper::canDelete<TYPE>(const Requester&,            \
                                            const uint64_t entity_id,    \
                                            Http::Error&);               \
  template drogon::Task<bool>                                            \
  GateKeeper::canCreateAsync<TYPE>(const Requester&,                     \
                                   const std::optional<jsoncons::json>&, \
                                   Http::Error&);                        \
  template drogon::Task<bool>                                            \
  GateKeeper::canReadAsync<TYPE>(const Requester&,                       \
                                 const uint64_t entity_id,               \
                                 Http::Error&);                          \
  template drogon::Task<bool>                                            \
  GateKeeper::canUpdateAsync<TYPE>(const Requester&,                     \
                                   const uint64_t entity_id,             \
                                   Http::Error&);                        \
  template drogon::Task<bool>                                            \
  GateKeeper::canDeleteAsync<TYPE>(const Requester&,                     \
                                   const uint64_t entity_id,             \
                                   Http::Error&);

// Builds on CRUD by adding the two client-only checks (get services, toggle
// active).
#define INSTANTIATE_GATEKEEPER_CLIENT(TYPE) /* NOLINT  */                     \
  INSTANTIATE_GATEKEEPER_CRUD(TYPE)                                           \
  template bool GateKeeper::canGetServices<TYPE>(const Requester& requester,  \
                                                 const uint64_t   _id,        \
                                                 Http::Error&     error);     \
  template bool GateKeeper::canToggleActive<TYPE>(const Requester& requester, \
                                                  const uint64_t   _id,       \
                                                  Http::Error&     error);    \
  template drogon::Task<bool>                                                 \
  GateKeeper::canGetServicesAsync<TYPE>(const Requester& requester,           \
                                        const uint64_t   _id,                 \
                                        Http::Error&     error);              \
  template drogon::Task<bool>                                                 \
  GateKeeper::canToggleActiveAsync<TYPE>(const Requester& requester,          \
                                         const uint64_t   _id,                \
                                         Http::Error&     error);

// Builds on CRUD by adding the service staff-management check.
#define INSTANTIATE_GATEKEEPER_ENTITY(TYPE) /* NOLINT  */                  \
  INSTANTIATE_GATEKEEPER_CRUD(TYPE)                                        \
  template bool GateKeeper::canManageStaff<TYPE>(const Requester&,         \
                                                 const uint64_t entity_id, \
                                                 Http::Error&);            \
  template drogon::Task<bool>                                              \
  GateKeeper::canManageStaffAsync<TYPE>(const Requester&,                  \
                                        const uint64_t entity_id,          \
                                        Http::Error&);

// Usage: expand one macro per supported entity type. CLIENT = User/Provider,
// ENTITY = the four service types, CRUD = the remaining
// data/records/appointment types.
INSTANTIATE_GATEKEEPER_CLIENT(
    User)  // Users can be authorized; gets CRUD + client-only checks.
INSTANTIATE_GATEKEEPER_CLIENT(Provider)  // Providers are the other client type.

INSTANTIATE_GATEKEEPER_ENTITY(
    Clinics)  // Clinic service: CRUD + staff management.
INSTANTIATE_GATEKEEPER_ENTITY(
    Pharmacies)  // Pharmacy service: CRUD + staff management.
INSTANTIATE_GATEKEEPER_ENTITY(
    Laboratories)  // Laboratory service: CRUD + staff management.
INSTANTIATE_GATEKEEPER_ENTITY(
    RadiologyCenters)  // Radiology service: CRUD + staff management.

INSTANTIATE_GATEKEEPER_CRUD(Patient)  // Patient record: the four basic checks.
INSTANTIATE_GATEKEEPER_CRUD(Health)   // Health record.
INSTANTIATE_GATEKEEPER_CRUD(PatientDrugs)           // Patient drugs record.
INSTANTIATE_GATEKEEPER_CRUD(Reports)                // Medical reports.
INSTANTIATE_GATEKEEPER_CRUD(Visits)                 // Visits.
INSTANTIATE_GATEKEEPER_CRUD(VisitDrugs)             // Drugs given in a visit.
INSTANTIATE_GATEKEEPER_CRUD(Prescriptions)          // Prescriptions.
INSTANTIATE_GATEKEEPER_CRUD(Requests)               // Requests.
INSTANTIATE_GATEKEEPER_CRUD(PaidServices)           // Paid services.
INSTANTIATE_GATEKEEPER_CRUD(ClinicAppointment)      // Clinic appointment.
INSTANTIATE_GATEKEEPER_CRUD(PharmacyAppointment)    // Pharmacy appointment.
INSTANTIATE_GATEKEEPER_CRUD(LaboratoryAppointment)  // Laboratory appointment.
INSTANTIATE_GATEKEEPER_CRUD(
    RadiologyCenterAppointment)  // Radiology appointment.
