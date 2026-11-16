/**
 * @file clientcontroller.cpp
 * @brief Definitions and explicit instantiations for ClientController<User> and
 * ClientController<Provider>.
 * @details The synchronous Create/Update paths offload password hashing to a
 * bounded PasswordWorker; all other CRUDS operations forward to
 * EntityController. Session bookkeeping (login/logout, suspend, delete) is
 *          owned by GateKeeper.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This file is where account behaviour is actually assembled. Slow password
 * work is handed to a worker so the IO loop stays responsive, while session
 * side effects are attached by wrapping callbacks. The coroutine CRUDS methods
 * simply reuse EntityController. Explicit instantiation at the bottom makes all
 * of this linkable for User and Provider, the two supported client types.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Lambdas and captures (copying the body into a worker job):
 * https://en.cppreference.com/w/cpp/language/lambda
 * - Concurrency / worker pools: https://en.cppreference.com/w/cpp/thread/thread
 * - Coroutines (co_await / co_return):
 * https://en.cppreference.com/w/cpp/language/coroutines
 * - Callback wrappers / std::function:
 * https://en.cppreference.com/w/cpp/utility/functional/function
 * - Move semantics (std::move):
 * https://en.cppreference.com/w/cpp/language/move_constructor
 * - Exceptions and catch blocks:
 * https://en.cppreference.com/w/cpp/language/exceptions
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */
// The class being defined.
#include "controllers/clientcontroller/clientcontroller.hpp"

#include <fmt/core.h>  // Error message formatting.

#include <cstdint>                  // For uint64_t client ids.
#include <exception>                // std::exception, caught around parsing.
#include <jsoncons/basic_json.hpp>  // JSON parsing and values.
#include <optional>                 // Optional ids/tokens.
#include <string>                   // std::string for copied request bodies.
#include <string_view>              // Non-owning view of request bodies.
#include <utility>                  // std::move for callbacks.

// The persistence methods invoked.
#include "controllers/base/controller/controller.hpp"
// Base CRUDS implementation reused here.
#include "controllers/entitycontroller/entitycontroller.hpp"
// Generic CRUDS interface.
#include "controllers/entitycontroller/entitycontrollerbase.hpp"
#include "drogon/utils/coroutine.h"  // Drogon Task type for Async methods.
#include "entities/base/types.hpp"   // Client data carriers.
// Provider entity, instantiated at the bottom.
#include "entities/people/provider.hpp"
#include "entities/people/user.hpp"  // User entity, instantiated at the bottom.
#include "utils/global/callback.hpp"  // Defines CALLBACK_.
// The Client_t concept used as constraint.
#include "utils/global/concepts.hpp"
#include "utils/global/http.hpp"            // HTTP status constants.
#include "utils/jsonhelper/jsonhelper.hpp"  // JSON helper utilities.
#include "validator/validator.hpp"          // Immutable-field validation rule.

// Client-specific data carriers and the suspended-state carrier used to build
// entities.
using HttpError = api::v2::Http::Error;  // Carries a status code and message
                                         // from validation/authorization.
using CreateClient_t =
    api::v2::Types::CreateClient_t;  // Carrier for validated create fields.
using UpdateClient_t =
    api::v2::Types::UpdateClient_t;  // Carrier for validated update fields.
using SuspendData =
    api::v2::Types::SuspendData;  // Carrier holding just the client id for
                                  // state toggles.
using Data_t =
    api::v2::Types::Data_t;  // Minimal id carrier used by service lookups.

template <Client_t T>
void ClientController<T>::Create(CALLBACK_&&       callback,
                                 const Requester&& requester,
                                 std::string_view  data) {
  // Copy the principal and body into the job: the queued lambda outlives this
  // request thread. A full queue makes runOnPasswordWorker answer 503 without
  // ever invoking createNow().
  runOnPasswordWorker(*passwordWorker,
                      std::move(callback),  // Queue the job; a full queue
                                            // answers 503 itself.
                      [this,
                       requester = Requester(requester),
                       body      = std::string(data)](CALLBACK_&& cb) mutable {
                        createNow(std::move(cb), requester, body);
                      });
}

template <Client_t T>
void ClientController<T>::createNow(CALLBACK_&&      callback,
                                    const Requester& requester,
                                    std::string_view data) {
  try {
    bool success =
        false;        // Validation flips this true only for a usable payload.
    HttpError error;  // Receives a code/message on validation or
                      // authorization failure.
    jsoncons::json
        json_data;  // Parsed body needed for the GateKeeper policy check.
    try {
      // The parsed form is needed for the GateKeeper policy check; bad
      // JSON is an immediate 400.
      json_data =
          jsoncons::json::parse(data);  // Turn the body text into a JSON value.
    } catch (const std::exception&) {
      callback(api::v2::Http::Status::BAD_REQUEST,
               "Invalid request body: malformed JSON.");
      return;
    }

    // Parses and validates the client-specific payload; `success` reports
    // whether the body was usable.
    CreateClient_t
        client_data(data,
                    T::getTableName(),
                    error,
                    success);  // Client-specific parse and validation.

    if (success) {
      T client(client_data);  // Build the entity that will be persisted.
      // Duplicates are a client error (409), reported before
      // authorization or any write.
      if (client.template exists<CreateClient_t>()) {
        callback(api::v2::Http::Status::CONFLICT,
                 "Client already exists");  // Duplicate id/username is a
                                            // client error.
        return;
      }

      // Authorization is the last gate before the INSERT; failures answer
      // with the gate's status.
      if (!gateKeeper->canCreate<T>(requester, json_data, error)) {
        callback(error.code, error.message);
        return;
      }

      Controller::Create(client,
                         std::move(callback));  // Delegate the INSERT to the
                                                // persistence engine.
    } else {
      callback(error.code,
               fmt::format("ClientData parsing error: {}.", error.message));
      return;
    }
  } catch (const std::exception& e) {
    CRITICALMESSAGERESPONSE  // Unexpected error: log it and answer with a
                             // mapped 5xx status.
  }
}

template <Client_t T>
void ClientController<T>::Read(CALLBACK_&&       callback,
                               const Requester&& requester,
                               std::string_view  data) {
  EntityController<T>::Read(std::move(callback),
                            std::move(requester),
                            data);  // Read needs no client-specific work.
}

template <Client_t T>
void ClientController<T>::Update(CALLBACK_&&                   callback,
                                 const Requester&&             requester,
                                 std::string_view              data,
                                 const std::optional<uint64_t> _id) {
  // As with Create, the job owns copies of the principal, body and id so it
  // can run after the request thread returns.
  runOnPasswordWorker(*passwordWorker,
                      std::move(callback),  // Queue the job; a full queue
                                            // answers 503 itself.
                      [this,
                       requester = Requester(requester),
                       body      = std::string(data),
                       _id](CALLBACK_&& cb) mutable {
                        updateNow(std::move(cb), requester, body, _id);
                      });
}

template <Client_t T>
void ClientController<T>::updateNow(CALLBACK_&&                   callback,
                                    const Requester&              requester,
                                    std::string_view              data,
                                    const std::optional<uint64_t> _id) {
  try {
    bool success =
        false;  // Validation flips this true only for a usable payload.
    // A client may not change its identity or password through the generic
    // update route.
    static const Validator::Rule rule(Validator::Rule::Action::ASSERT_IMMUTABLE,
                                      {"username", "password"});

    HttpError      error;  // Receives a code/message on validation or
                           // authorization failure.
    UpdateClient_t client_data(data,
                               _id,
                               T::getTableName(),
                               error,
                               success,
                               rule);

    if (success) {
      T client(client_data);  // Build the entity that will be persisted.
      // Authorize against the target row before delegating the write.
      if (!gateKeeper->canUpdate<T>(requester, _id.value(), error)) {
        callback(error.code, error.message);
        return;
      }
      Controller::Update(client,
                         std::move(callback));  // Delegate the UPDATE to the
                                                // persistence engine.
    } else {
      callback(error.code,
               fmt::format("ClientData parsing error: {}.", error.message));
      return;
    }
  } catch (const std::exception& e) {
    CRITICALMESSAGERESPONSE  // Unexpected error: log it and answer with a
                             // mapped 5xx status.
  }
}

template <Client_t T>
void ClientController<T>::Delete(CALLBACK_&&                   callback,
                                 const Requester&&             requester,
                                 const std::optional<uint64_t> client_id) {
  auto cb = std::move(callback);  // Take ownership so it can be captured into
                                  // the wrapper lambda.
  // Intercept the result so sessions are dropped only when the delete
  // actually succeeded; then forward unchanged.
  CALLBACK_ wrapped = [this,
                       client_id,
                       cb = std::move(cb)](int                code,
                                           const std::string& body) mutable {
    if (code == static_cast<int>(api::v2::Http::Status::OK) &&
        client_id.has_value()) {
      gateKeeper->removeSession(client_id,
                                T::getTableName());  // A suspended client must
                                                     // not keep a live session.
    }
    std::move(cb)(code, body);
  };
  EntityController<T>::Delete(std::move(wrapped),
                              std::move(requester),
                              client_id);
}

template <Client_t T>
void ClientController<T>::Search(CALLBACK_&&       callback,
                                 const Requester&& requester,
                                 std::string_view  data) {
  EntityController<T>::Search(std::move(callback),
                              std::move(requester),
                              data);  // Search needs no client-specific work.
}

template <Client_t T>
void ClientController<T>::Login(CALLBACK_&&                        callback,
                                [[maybe_unused]] const Requester&& requester,
                                std::string_view                   data,
                                const std::string&                 ip_address) {
  // Authentication and session creation are entirely GateKeeper's
  // responsibility; requester is unused.
  gateKeeper->login(std::move(callback),
                    data,
                    ip_address,
                    T::getTableName());  // GateKeeper owns authentication and
                                         // session state.
}

template <Client_t T>
drogon::Task<void> ClientController<T>::LoginAsync(
    CALLBACK_&&                        callback,
    [[maybe_unused]] const Requester&& requester,
    std::string_view                   data,
    const std::string&                 ip_address) {
  // Await the gate's async login; the token/session state lives in
  // GateKeeper, not in this controller.
  co_await gateKeeper->loginAsync(std::move(callback),
                                  data,
                                  ip_address,
                                  T::getTableName());  // Await the async login.
}

template <Client_t T>
void ClientController<T>::Logout(CALLBACK_&&                        callback,
                                 [[maybe_unused]] const Requester&& requester,
                                 const std::optional<std::string>&  token,
                                 const std::string& ip_address) {
  // Session invalidation is delegated to GateKeeper; requester is unused.
  gateKeeper->logout(std::move(callback),
                     token,
                     ip_address,
                     T::getTableName());  // GateKeeper invalidates the session.
}

template <Client_t T>
void ClientController<T>::Suspend(CALLBACK_&&                   callback,
                                  const Requester&&             requester,
                                  const std::optional<uint64_t> client_id) {
  try {
    // No id means no account can be toggled; reject with 406 before any
    // authorization or DB work.
    if (!client_id.has_value()) {
      callback(api::v2::Http::Status::NOT_ACCEPTABLE, "Invalid id provided");
      return;
    }

    SuspendData suspendData(client_id.value());
    T         client(suspendData);  // Minimal entity carrying the id to toggle.
    HttpError error;

    // Suspend and Activate share the toggle-active permission; denial
    // short-circuits with the gate's status.
    if (!gateKeeper->canToggleActive<T>(requester, client_id.value(), error)) {
      callback(error.code, error.message);
      return;
    }

    Controller::Suspend(client,
                        std::move(callback));  // Issue the deactivation update.
    // A suspended client must not keep a usable session; drop it after the
    // state change is issued.
    gateKeeper->removeSession(client_id,
                              T::getTableName());  // A suspended client must
                                                   // not keep a live session.
  } catch (const std::exception& e) {
    CRITICALMESSAGERESPONSE  // Unexpected error: log it and answer with a
                             // mapped 5xx status.
  }
}

template <Client_t T>
drogon::Task<void> ClientController<T>::SuspendAsync(
    CALLBACK_&&                   callback,
    const Requester&&             requester,
    const std::optional<uint64_t> client_id) {
  try {
    // No id means no account can be toggled; reject with 406 before any
    // authorization or DB work.
    if (!client_id.has_value()) {
      std::move(callback)(api::v2::Http::Status::NOT_ACCEPTABLE,
                          "Invalid id provided");
      co_return;
    }

    SuspendData suspendData(client_id.value());
    T         client(suspendData);  // Minimal entity carrying the id to toggle.
    HttpError error;

    // Suspend and Activate share the toggle-active permission; awaited on
    // the IO loop.
    if (!co_await gateKeeper->canToggleActiveAsync<T>(requester,
                                                      client_id.value(),
                                                      error)) {
      std::move(callback)(error.code, error.message);
      co_return;
    }

    co_await Controller::SuspendAsync(client, std::move(callback));
    // Invalidate sessions once the suspension has been applied.
    gateKeeper->removeSession(client_id,
                              T::getTableName());  // A suspended client must
                                                   // not keep a live session.
  } catch (const std::exception& e) {
    CRITICALMESSAGERESPONSE  // Unexpected error: log it and answer with a
                             // mapped 5xx status.
  }
}

template <Client_t T>
void ClientController<T>::Activate(CALLBACK_&&                   callback,
                                   const Requester&&             requester,
                                   const std::optional<uint64_t> client_id) {
  try {
    // No id means no account can be toggled; reject with 406 before any
    // authorization or DB work.
    if (!client_id.has_value()) {
      callback(api::v2::Http::Status::NOT_ACCEPTABLE, "Invalid id provided");
      return;
    }
    SuspendData suspendData(client_id.value());

    T client(suspendData);

    HttpError error;
    // Suspend and Activate share the toggle-active permission; denial
    // short-circuits with the gate's status.
    if (!gateKeeper->canToggleActive<T>(requester, client_id.value(), error)) {
      callback(error.code, error.message);
      return;
    }

    // Reactivation only flips the active flag; unlike Suspend, this path
    // does not touch sessions.
    Controller::Unsuspend(client,
                          std::move(callback));  // Reactivation only flips the
                                                 // active flag.
  } catch (const std::exception& e) {
    CRITICALMESSAGERESPONSE  // Unexpected error: log it and answer with a
                             // mapped 5xx status.
  }
}

template <Client_t T>
drogon::Task<void> ClientController<T>::ActivateAsync(
    CALLBACK_&&                   callback,
    const Requester&&             requester,
    const std::optional<uint64_t> client_id) {
  try {
    // No id means no account can be toggled; reject with 406 before any
    // authorization or DB work.
    if (!client_id.has_value()) {
      std::move(callback)(api::v2::Http::Status::NOT_ACCEPTABLE,
                          "Invalid id provided");
      co_return;
    }
    SuspendData suspendData(client_id.value());

    T client(suspendData);

    HttpError error;
    // Suspend and Activate share the toggle-active permission; awaited on
    // the IO loop.
    if (!co_await gateKeeper->canToggleActiveAsync<T>(requester,
                                                      client_id.value(),
                                                      error)) {
      std::move(callback)(error.code, error.message);
      co_return;
    }

    // Reactivation does not touch sessions (unlike SuspendAsync).
    co_await Controller::UnsuspendAsync(client, std::move(callback));
  } catch (const std::exception& e) {
    CRITICALMESSAGERESPONSE  // Unexpected error: log it and answer with a
                             // mapped 5xx status.
  }
}

template <Client_t T>
void ClientController<T>::ResetPassword(
    CALLBACK_&&                        callback,
    [[maybe_unused]] const Requester&& requester,
    std::string_view                   data) {
  // Not implemented: both arguments are discarded, so the callback is never
  // invoked by this method.
  (void)std::move(callback);  // Explicitly discard the unused callback (no
                              // answer is sent).
  (void)data;                 // Explicitly discard the unused body.
}

template <Client_t T>
void ClientController<T>::GetServices(CALLBACK_&&             callback,
                                      const Requester&&       requester,
                                      std::optional<uint64_t> client_id) {
  try {
    // Without a client id there is nothing to query; reject with 400 before
    // authorization.
    if (!client_id.has_value()) {
      callback(api::v2::Http::Status::BAD_REQUEST,
               "client_id extraction failed");
      return;
    }

    T client(
        Data_t(client_id.value()));  // Minimal entity carrying the client id.

    HttpError error;
    // The requester may only list services it is entitled to see for this
    // client.
    if (!gateKeeper->canGetServices<T>(requester, client_id.value(), error)) {
      callback(error.code, error.message);
      return;
    }

    Controller::GetServices(client,
                            std::move(callback));  // Delegate the service
                                                   // lookup query.
  } catch (const std::exception& e) {
    CRITICALMESSAGERESPONSE  // Unexpected error: log it and answer with a
                             // mapped 5xx status.
  }
}

template <Client_t T>
drogon::Task<void> ClientController<T>::GetServicesAsync(
    CALLBACK_&&             callback,
    const Requester&&       requester,
    std::optional<uint64_t> client_id) {
  try {
    // Without a client id there is nothing to query; reject with 400 before
    // authorization.
    if (!client_id.has_value()) {
      std::move(callback)(api::v2::Http::Status::BAD_REQUEST,
                          "client_id extraction failed");
      co_return;
    }

    T client(
        Data_t(client_id.value()));  // Minimal entity carrying the client id.

    HttpError error;
    // Async entitlement check; a denial short-circuits with the gate's
    // status.
    if (!co_await gateKeeper->canGetServicesAsync<T>(requester,
                                                     client_id.value(),
                                                     error)) {
      std::move(callback)(error.code, error.message);
      co_return;
    }

    co_await Controller::GetServicesAsync(client,
                                          std::move(
                                              callback));  // Await the async
                                                           // service lookup.
  } catch (const std::exception& e) {
    CRITICALMESSAGERESPONSE  // Unexpected error: log it and answer with a
                             // mapped 5xx status.
  }
}

// The coroutine CRUDS forwards below reuse EntityController's generic
// implementations. DeleteAsync wraps the callback first so the session cleanup
// on success matches the synchronous Delete().
template <Client_t T>
drogon::Task<void> ClientController<T>::CreateAsync(CALLBACK_&&       callback,
                                                    const Requester&& requester,
                                                    std::string_view  data) {
  co_await EntityController<T>::CreateAsync(std::move(callback),
                                            std::move(requester),
                                            data);  // Async create reuses the
                                                    // generic pipeline.
}

template <Client_t T>
drogon::Task<void> ClientController<T>::ReadAsync(CALLBACK_&&       callback,
                                                  const Requester&& requester,
                                                  std::string_view  data) {
  co_await EntityController<T>::ReadAsync(std::move(callback),
                                          std::move(requester),
                                          data);  // Async read reuses the
                                                  // generic pipeline.
}

template <Client_t T>
drogon::Task<void> ClientController<T>::UpdateAsync(
    CALLBACK_&&                   callback,
    const Requester&&             requester,
    std::string_view              data,
    const std::optional<uint64_t> _id) {
  co_await EntityController<T>::UpdateAsync(std::move(callback),
                                            std::move(requester),
                                            data,
                                            _id);  // Async update reuses the
                                                   // generic pipeline.
}

template <Client_t T>
drogon::Task<void> ClientController<T>::DeleteAsync(
    CALLBACK_&&                   callback,
    const Requester&&             requester,
    const std::optional<uint64_t> client_id) {
  auto cb = std::move(callback);  // Take ownership so it can be captured into
                                  // the wrapper lambda.
  CALLBACK_ wrapped = [this,
                       client_id,
                       cb = std::move(cb)](int                code,
                                           const std::string& body) mutable {
    if (code == static_cast<int>(api::v2::Http::Status::OK) &&
        client_id.has_value()) {
      gateKeeper->removeSession(client_id,
                                T::getTableName());  // A suspended client must
                                                     // not keep a live session.
    }
    std::move(cb)(code, body);
  };
  co_await EntityController<T>::
      DeleteAsync(std::move(wrapped),
                  std::move(requester),
                  client_id);  // Wrapper drops sessions on a successful delete.
}

template <Client_t T>
drogon::Task<void> ClientController<T>::SearchAsync(CALLBACK_&&       callback,
                                                    const Requester&& requester,
                                                    std::string_view  data) {
  co_await EntityController<T>::SearchAsync(std::move(callback),
                                            std::move(requester),
                                            data);  // Async search reuses the
                                                    // generic pipeline.
}
/**
 * @def INSTANTIATE_CLIENT_CONTROLLER(TYPE)
 * @brief Instantiates every ClientController member (CRUDS, auth, state and
 * services) for one client type.
 * @details Needed because definitions live in this translation unit; callers
 * including only the header must be able to link. User and Provider are the
 * supported client types.
 */
#define INSTANTIATE_CLIENT_CONTROLLER(                                           \
    TYPE) /* NOLINT(cppcoreguidelines-macro-usage)*/                             \
  template void ClientController<TYPE>::Create(CALLBACK_&&       callback,       \
                                               const Requester&& requester,      \
                                               std::string_view  data);          \
  template void ClientController<TYPE>::Read(CALLBACK_&&       callback,         \
                                             const Requester&& requester,        \
                                             std::string_view  data);            \
  template void ClientController<TYPE>::Update(CALLBACK_&&       callback,       \
                                               const Requester&& requester,      \
                                               std::string_view  data,           \
                                               std::optional<uint64_t> id);      \
  template void                                                                  \
  ClientController<TYPE>::Delete(CALLBACK_&&             callback,               \
                                 const Requester&&       requester,              \
                                 std::optional<uint64_t> client_id);             \
  template void ClientController<TYPE>::Search(CALLBACK_&&       callback,       \
                                               const Requester&& requester,      \
                                               std::string_view  data);          \
  template drogon::Task<void>                                                    \
  ClientController<TYPE>::CreateAsync(CALLBACK_&&       callback,                \
                                      const Requester&& requester,               \
                                      std::string_view  data);                   \
  template drogon::Task<void>                                                    \
  ClientController<TYPE>::ReadAsync(CALLBACK_&&       callback,                  \
                                    const Requester&& requester,                 \
                                    std::string_view  data);                     \
  template drogon::Task<void>                                                    \
  ClientController<TYPE>::UpdateAsync(CALLBACK_&&             callback,          \
                                      const Requester&&       requester,         \
                                      std::string_view        data,              \
                                      std::optional<uint64_t> id);               \
  template drogon::Task<void>                                                    \
  ClientController<TYPE>::DeleteAsync(CALLBACK_&&             callback,          \
                                      const Requester&&       requester,         \
                                      std::optional<uint64_t> client_id);        \
  template drogon::Task<void>                                                    \
                ClientController<TYPE>::SearchAsync(CALLBACK_&&       callback,  \
                                                    const Requester&& requester, \
                                                    std::string_view  data);     \
  template void ClientController<TYPE>::Login(CALLBACK_&&        callback,       \
                                              const Requester&&  requester,      \
                                              std::string_view   data,           \
                                              const std::string& ip_address);    \
  template drogon::Task<void>                                                    \
  ClientController<TYPE>::LoginAsync(CALLBACK_&&        callback,                \
                                     const Requester&&  requester,               \
                                     std::string_view   data,                    \
                                     const std::string& ip_address);             \
  template void                                                                  \
  ClientController<TYPE>::Logout(CALLBACK_&&                       callback,     \
                                 const Requester&&                 requester,    \
                                 const std::optional<std::string>& token,        \
                                 const std::string&                ip_address);  \
  template void                                                                  \
  ClientController<TYPE>::Suspend(CALLBACK_&&             callback,              \
                                  const Requester&&       requester,             \
                                  std::optional<uint64_t> client_id);            \
  template void                                                                  \
  ClientController<TYPE>::Activate(CALLBACK_&&             callback,             \
                                   const Requester&&       requester,            \
                                   std::optional<uint64_t> client_id);           \
  template void                                                                  \
  ClientController<TYPE>::ResetPassword(CALLBACK_&&       callback,              \
                                        const Requester&& requester,             \
                                        std::string_view  data);                 \
  template drogon::Task<void>                                                    \
  ClientController<TYPE>::SuspendAsync(CALLBACK_&&             callback,         \
                                       const Requester&&       requester,        \
                                       std::optional<uint64_t> client_id);       \
  template drogon::Task<void>                                                    \
  ClientController<TYPE>::ActivateAsync(CALLBACK_&&             callback,        \
                                        const Requester&&       requester,       \
                                        std::optional<uint64_t> client_id);      \
  template void                                                                  \
  ClientController<TYPE>::GetServices(CALLBACK_&&             callback,          \
                                      const Requester&&       requester,         \
                                      std::optional<uint64_t> client_id);        \
  template drogon::Task<void>                                                    \
  ClientController<TYPE>::GetServicesAsync(CALLBACK_&&             callback,     \
                                           const Requester&&       requester,    \
                                           std::optional<uint64_t> client_id);

INSTANTIATE_CLIENT_CONTROLLER(User)      // User client type.
INSTANTIATE_CLIENT_CONTROLLER(Provider)  // Provider client type.
