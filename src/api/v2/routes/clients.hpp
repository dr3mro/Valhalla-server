/**
 * @file clients.hpp
 * @brief REST controller for client accounts (users and providers).
 * @details This file defines the Clients controller, which manages user and
 * provider accounts. Its endpoints cover account creation, login and logout,
 * suspend/activate, read, update, delete, search and listing a client's
 * services. A {clientType} part of the URL (users or providers) selects one
 * concrete ClientController from a std::variant registry. Create and login are
 *          INSECURE (no token needed); the rest are SECURE and use the identity
 * attributes set by Auth. Most handlers either await async controller
 * coroutines or use executeControllerMethod; read also has a prototype
 * asynchronous implementation for users and providers.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This controller is the HTTP front door for client accounts. Drogon routes the
 * /api/v2 clients URLs here, and its handlers call the shared dispatcher, the
 * GateKeeper, the Validator and a DatabaseController to reach the database. It
 * is the only place that issues and invalidates login tokens (through the
 * login/logout controllers), so it underpins authentication for the whole API.
 * Without it clients could not register, sign in or manage their accounts.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - REST create/read/update/delete (CRUD) routes:
 * https://developer.mozilla.org/en-US/docs/Web/HTTP/Methods
 * - Bearer tokens and the Authorization header:
 * https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Authorization
 * - HTTP filters/middleware: https://drogon.docsforge.com/master/filter/
 * - C++ coroutines (co_await):
 * https://en.cppreference.com/w/cpp/language/coroutines
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Only process this header once per source file.
// Drogon controller base and the request/response/status types used by the
// handlers.
// Base class drogon::HttpController used by Clients.
#include <drogon/HttpController.h>
// Request type carrying body, attributes and URL parameters.
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>  // Response type built by the JSON helpers.
#include <drogon/HttpTypes.h>     // HTTP verb and status constants.

#include <functional>     // std::function is the type of the response callback.
#include <memory>         // std::shared_ptr for the shared controller objects.
#include <string>         // std::string for URLs, keys and messages.
#include <string_view>    // std::string_view keys the registry map cheaply.
#include <unordered_map>  // The registry maps type names to controllers.
#include <utility>        // std::move for consuming callbacks.
// std::variant holds one of several controller pointer types.
#include <variant>

// The next line is marked for IWYU so the include is not removed: this header
// needs the shared helpers.
#include "api/v2/basic/common.hpp"  // IWYU pragma: keep
// Reads DatabaseConfig::async to choose async or blocking mode.
#include "configurator/configurator.hpp"
// ClientController<T> template definition.
#include "controllers/clientcontroller/clientcontroller.hpp"
// Base class exposing the shared method pointers.
#include "controllers/clientcontroller/clientcontrollerbase.hpp"
// DatabaseController runs the async read query.
#include "controllers/databasecontroller/databasecontroller.hpp"
// Async DB client used by DatabaseController.
#include "database/asyncdbclient.hpp"
// DatabaseBusy exception mapped to a retryable 503.
#include "database/databasebusy.hpp"
// DbExecutor runs blocking database work off the IO thread.
#include "database/dbexecutor.hpp"
// drogon::async_run and drogon::Task for async paths.
#include "drogon/utils/coroutine.h"
#include "entities/people/provider.hpp"  // Provider entity type.
#include "entities/people/user.hpp"      // User entity type.
// GateKeeper performs CPU-only permission checks.
#include "gatekeeper/gatekeeper.hpp"
#include "store/store.hpp"  // Store/service locator supplies shared objects.
#include "utils/global/http.hpp"  // api::v2::Http::Status numeric result codes.
// Validator checks read schemas in the async read path.
#include "validator/validator.hpp"

// The v2 API controllers live here.
namespace api::v2 {
  /**
   * @class Clients
   * @brief REST controller for client accounts (users and providers).
   * @details A {clientType} path segment selects a ClientController<User> or
   * ClientController<Provider> from a variant registry. create and login are
   * INSECURE (no token required); all other routes are SECURE and use the
   * identity attributes inserted by Auth. Most handlers delegate to
   * executeControllerMethod; several provide a drogon::Task coroutine path
   * when DatabaseConfig::async is set, and read has a prototype async
   * implementation for users and providers only. Controllers, GateKeeper,
   * DatabaseController and the executor are shared service-locator objects.
   */
  class Clients
      // Inherit Drogon's controller
      // template so Drogon
      // registers these routes.
      : public drogon::HttpController<Clients> {
   public:
    /**
     * @brief Create a user or provider account from the JSON request body.
     * @details POST /{clientType}/create (INSECURE - no authentication
     * required). Dispatched via executeControllerMethod with the
     * synchronous Create method; unknown clientType yields 400.
     * @param[in] req Request carrying the body.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] clientType Registry key ("users" or "providers").
     */
    // HTTP handler creating a
    // client account from the body.
    void create(const drogon::HttpRequestPtr&                         req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                const std::string& clientType) {
      executeControllerMethod(*dbExecutor,
                              clientRegistry,
                              clientType,
                              &ClientControllerBase::Create,
                              req,
                              std::move(callback),
                              req->body());  // Always blocking: shared
                                             // dispatcher runs Create with the
                                             // body.
    }

    /**
     * @brief Authenticate a client and issue a token.
     * @details POST /{clientType}/login (INSECURE). The async path runs
     * LoginAsync in a coroutine with an unauthenticated Requester(0, "")
     * and the peer IP; the sync path uses executeControllerMethod with
     * Login and the peer IP. Unknown clientType yields 400.
     * @param[in] req Request carrying credentials and peer address.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] clientType Registry key ("users" or "providers").
     */
    // HTTP handler that checks
    // credentials and returns a token.
    void login(const drogon::HttpRequestPtr&                         req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& clientType) {
      const bool async =
          asyncDb_;  // Copy cached flag selecting the database mode.
      // Async mode: run the controller coroutine.
      if (async) {
        auto cb = std::move(callback);  // Move callback into coroutine.
        // Start coroutine capturing
        // state by value.
        drogon::async_run([this, req, cb, clientType]() mutable
                              -> drogon::Task<void> {
          // Adapter: controller status
          // -> HTTP response.
          CALLBACK_ mcb = [cb](int code, const std::string& content) {
            using Reply = std::function<void(
                // Response callback alias.
                const drogon::HttpResponsePtr&)>;
            // Map numeric status to reply.
            switch (code) {
              // Success.
              case api::v2::Http::Status::OK:
                Helper::successResponse(content,
                                        Reply(cb));  // Send 200 with body.
                break;                               // Leave the switch.
              case api::v2::Http::Status::INTERNAL_SERVER_ERROR:  // Failure.
                Helper::failureResponse(content,
                                        Reply(cb));  // Send tagged 500.
                break;                               // Leave the switch.
              default:                               // Other error.
                Helper::errorResponse(static_cast<drogon::HttpStatusCode>(code),
                                      content,
                                      Reply(cb));  // Reuse numeric status.
                break;                             // Leave the switch.
            }
          };

          Requester requester(0,
                              "");  // login has no authenticated requester

          auto ctlIt = clientRegistry.find(
              clientType);  // Find controller for this URL type.
          // Unknown type.
          if (ctlIt == clientRegistry.end()) {
            std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                           fmt::format("Type mapping not found for: {}",
                                       clientType));  // Reply 400.
            co_return;                                // End coroutine.
          }

          // Guard the controller call.
          try {
            co_await std::visit(
                [&](auto& controller)
                    -> drogon::Task<void> {  // Dispatch to correct
                                             // controller type.
                  // Log in asynchronously
                  // with body and peer IP.
                  // Selected variant.
                  // Unexpected error.
                  co_await controller->LoginAsync(std::move(mcb),
                                                  std::move(requester),
                                                  req->body(),
                                                  req->peerAddr().toIp());
                },
                ctlIt->second);
          } catch (const std::exception& e) {
            Helper::failureResponse(e.what(), std::move(cb));  // Reply 500.
          }
        });
        return;  // Coroutine owns the rest.
      }

      executeControllerMethod(*dbExecutor,
                              clientRegistry,
                              clientType,
                              &ClientControllerBase::Login,
                              req,
                              std::move(callback),
                              req->body(),
                              req->peerAddr()
                                  .toIp());  // Blocking mode: dispatch Login
                                             // with body and peer IP.
    }
    /**
     * @brief Invalidate the token carried in the Authorization header.
     * @details POST /{clientType}/logout (SECURE). Strips the 7-character
     * "Bearer " prefix from the header and forwards the token plus peer IP
     * through executeControllerMethod.
     * @param[in] req Request carrying the Authorization header and peer
     * address.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] clientType Registry key.
     */
    // HTTP handler that invalidates
    // the current token.
    void logout(const drogon::HttpRequestPtr&                         req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                const std::string& clientType) {
      executeControllerMethod(*dbExecutor,
                              clientRegistry,
                              clientType,
                              &ClientControllerBase::Logout,
                              req,
                              std::move(
                                  callback),  // Blocking dispatch; forwarded
                                              // arguments follow.
                              req->getHeader("Authorization").substr(7),
                              req->peerAddr().toIp());  // NOLINT
    }

    /**
     * @brief Suspend the client identified by the "id" query parameter.
     * @details POST /{clientType}/suspend (SECURE). A bad id yields 400 and
     * returns early; the async path co_awaits SuspendAsync, the sync path
     * uses executeControllerMethod.
     * @param[in] req Request carrying the id parameter and Auth identity
     * attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] clientType Registry key.
     */
    // HTTP handler that suspends a client.
    void suspend(const drogon::HttpRequestPtr&                         req,
                 std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                 const std::string& clientType) {
      // Reject a bad id with 400 before scheduling the async coroutine.
      auto id =
          parseIdParam(req, "id", callback);  // Parse id; 400 sent on failure.
      if (!id)   // No id means error response was sent.
        return;  // Leave early.

      const bool async = asyncDb_;  // Which database mode is configured.
      // Async coroutine path.
      if (async) {
        auto cb = std::move(callback);  // Move callback into coroutine.
        drogon::async_run([this,
                           req,
                           cb,
                           clientType,
                           // Capture id too.
                           id]() mutable -> drogon::Task<void> {
          // Adapt status to HTTP.
          CALLBACK_ mcb = [cb](int code, const std::string& content) {
            using Reply = std::function<void(
                // Response callback alias.
                const drogon::HttpResponsePtr&)>;
            // Map status to reply.
            switch (code) {
              // Success.
              case api::v2::Http::Status::OK:
                Helper::successResponse(
                    // Send 200.
                    content,
                    Reply(cb));
                break;  // Leave the switch.
              case api::v2::Http::Status::INTERNAL_SERVER_ERROR:  // Failure.
                Helper::failureResponse(content,
                                        Reply(cb));  // Send tagged 500.
                break;                               // Leave the switch.
              default:                               // Other error.
                Helper::errorResponse(static_cast<drogon::HttpStatusCode>(code),
                                      content,
                                      Reply(cb));  // Reuse numeric status.
                break;                             // Leave the switch.
            }
          };

          Requester requester(req->getAttributes()->get<uint64_t>("clientID"),
                              req->getAttributes()->get<std::string>(
                                  "clientGroup"));  // Caller identity for
                                                    // permissions.

          auto ctlIt =
              clientRegistry.find(clientType);  // Look up controller for type.
          // Unknown type.
          if (ctlIt == clientRegistry.end()) {
            std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                           fmt::format("Type mapping not found for: {}",
                                       clientType));  // Reply 400.
            co_return;                                // Stop.
          }

          // Guard the call.
          try {
            co_await std::visit(
                [&](auto& controller)
                    -> drogon::Task<void> {  // Dispatch to right controller.
                  // Suspend asynchronously by id.
                  // Chosen variant.
                  // Unexpected error.
                  co_await controller->SuspendAsync(std::move(mcb),
                                                    std::move(requester),
                                                    id);
                },
                ctlIt->second);
          } catch (const std::exception& e) {
            Helper::failureResponse(e.what(), std::move(cb));  // Reply 500.
          }
        });

        return;  // Coroutine handles rest.
      }

      executeControllerMethod(*dbExecutor,
                              clientRegistry,
                              clientType,
                              &ClientControllerBase::Suspend,
                              req,
                              std::move(callback),
                              *id);  // Blocking mode: dispatch Suspend by id.
    }

    /**
     * @brief Activate the client identified by the "id" query parameter.
     * @details POST /{clientType}/activate (SECURE). A bad id yields 400
     * and returns early; the async path co_awaits ActivateAsync, the sync
     * path uses executeControllerMethod.
     * @param[in] req Request carrying the id parameter and Auth identity
     * attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] clientType Registry key.
     */
    // HTTP handler that activates a client.
    void activate(
        const drogon::HttpRequestPtr&                         req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string&                                    clientType) {
      // Reject a bad id with 400 before scheduling the async coroutine.
      auto id =
          parseIdParam(req, "id", callback);  // Parse id; 400 sent on failure.
      if (!id)   // No id means error response was sent.
        return;  // Leave early.

      const bool async = asyncDb_;  // Which database mode is configured.
      // Async coroutine path.
      if (async) {
        auto cb = std::move(callback);  // Move callback into coroutine.
        drogon::async_run([this,
                           req,
                           cb,
                           clientType,
                           // Capture id too.
                           id]() mutable -> drogon::Task<void> {
          // Adapt status to HTTP.
          CALLBACK_ mcb = [cb](int code, const std::string& content) {
            using Reply = std::function<void(
                // Response callback alias.
                const drogon::HttpResponsePtr&)>;
            // Map status to reply.
            switch (code) {
              // Success.
              case api::v2::Http::Status::OK:
                Helper::successResponse(
                    // Send 200.
                    content,
                    Reply(cb));
                break;  // Leave the switch.
              case api::v2::Http::Status::INTERNAL_SERVER_ERROR:  // Failure.
                Helper::failureResponse(content,
                                        Reply(cb));  // Send tagged 500.
                break;                               // Leave the switch.
              default:                               // Other error.
                Helper::errorResponse(static_cast<drogon::HttpStatusCode>(code),
                                      content,
                                      Reply(cb));  // Reuse numeric status.
                break;                             // Leave the switch.
            }
          };

          Requester requester(req->getAttributes()->get<uint64_t>("clientID"),
                              req->getAttributes()->get<std::string>(
                                  "clientGroup"));  // Caller identity for
                                                    // permissions.

          auto ctlIt =
              clientRegistry.find(clientType);  // Look up controller for type.
          // Unknown type.
          if (ctlIt == clientRegistry.end()) {
            std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                           fmt::format("Type mapping not found for: {}",
                                       clientType));  // Reply 400.
            co_return;                                // Stop.
          }

          // Guard the call.
          try {
            co_await std::visit(
                [&](auto& controller)
                    -> drogon::Task<void> {  // Dispatch to right controller.
                  // Activate asynchronously by
                  // id.
                  // Chosen variant.
                  // Unexpected error.
                  co_await controller->ActivateAsync(std::move(mcb),
                                                     std::move(requester),
                                                     id);
                },
                ctlIt->second);
          } catch (const std::exception& e) {
            Helper::failureResponse(e.what(), std::move(cb));  // Reply 500.
          }
        });

        return;  // Coroutine handles rest.
      }

      executeControllerMethod(*dbExecutor,
                              clientRegistry,
                              clientType,
                              &ClientControllerBase::Activate,
                              req,
                              std::move(callback),
                              *id);  // Blocking mode: dispatch Activate by id.
    }

    /**
     * @brief Read a client entity selected by id and schema in the JSON
     * body.
     * @details POST /{clientType}/read (SECURE). When DatabaseConfig::async
     * is set and the type is users or providers, the prototype
     * readAsyncImpl<T> coroutine is used; all other cases delegate to
     * executeControllerMethod.
     * @param[in] req Request carrying the body and Auth identity
     * attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] clientType Registry key.
     */
    // HTTP handler that reads one client entity.
    void read(const drogon::HttpRequestPtr&                         req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
              const std::string& clientType) {
      // Prototype async path (DB_ASYNC=true, users and providers): the
      // read runs as a coroutine on the IO loop.
      const bool async = asyncDb_;  // Which database mode is configured.
      // Use the prototype only for these
      // two types in async mode.
      if (async && (clientType == "users" || clientType == "providers")) {
        auto cb = std::move(callback);  // Move callback into coroutine.
        // Select the User specialization.
        if (clientType == "users") {
          drogon::async_run(
              [this, req, cb]() mutable
                  -> drogon::Task<void> {  // Launch the user read coroutine.
                // Run the templated
                // async read for User.
                // The only other type here is providers.
                co_await readAsyncImpl<User>(req, std::move(cb), "users");
              });
        } else {
          drogon::async_run([this, req, cb]() mutable
                                -> drogon::Task<void> {  // Launch the provider
                                                         // read coroutine.
            co_await readAsyncImpl<Provider>(req,
                                             std::move(cb),
                                             "providers");  // Run the templated
                                                            // async read for
                                                            // Provider.
          });
        }
        return;  // Coroutine handles rest.
      }
      executeControllerMethod(*dbExecutor,
                              clientRegistry,
                              clientType,
                              &ClientControllerBase::Read,
                              req,
                              std::move(callback),
                              req->body());  // Other cases: blocking dispatch
                                             // of Read with body.
    }

    /**
     * @brief Update the client identified by the "id" query parameter.
     * @details PUT /{clientType}/update (SECURE). A bad id yields 400 and
     * returns early; the work is always dispatched synchronously through
     * executeControllerMethod.
     * @param[in] req Request carrying the body, id parameter and Auth
     * identity attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] clientType Registry key.
     */
    // HTTP handler that updates a client.
    void update(const drogon::HttpRequestPtr&                         req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                const std::string& clientType) {
      // Reject a bad id with 400 before dispatching.
      auto id =
          parseIdParam(req, "id", callback);  // Parse id; 400 sent on failure.
      if (!id)   // No id means error response was sent.
        return;  // Leave early.
      executeControllerMethod(*dbExecutor,
                              clientRegistry,
                              clientType,
                              &ClientControllerBase::Update,
                              req,
                              std::move(callback),
                              req->body(),
                              *id);  // Blocking dispatch of Update with body
                                     // and id.
    }
    /**
     * @brief Delete the client identified by the "id" query parameter.
     * @details DELETE /{clientType}/delete (SECURE). A bad id yields 400
     * and returns early; the async path co_awaits DeleteAsync, the sync
     * path uses executeControllerMethod.
     * @param[in] req Request carrying the id parameter and Auth identity
     * attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] clientType Registry key.
     */
    // HTTP handler that deletes a client
    // (trailing _ avoids the C++ keyword).
    void delete_(const drogon::HttpRequestPtr&                         req,
                 std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                 const std::string& clientType) {
      // Reject a bad id with 400 before scheduling the async coroutine.
      auto id =
          parseIdParam(req, "id", callback);  // Parse id; 400 sent on failure.
      if (!id)   // No id means error response was sent.
        return;  // Leave early.

      const bool async = asyncDb_;  // Which database mode is configured.
      // Async coroutine path.
      if (async) {
        auto cb = std::move(callback);  // Move callback into coroutine.
        drogon::async_run([this,
                           req,
                           cb,
                           clientType,
                           // Capture id too.
                           id]() mutable -> drogon::Task<void> {
          // Adapt status to HTTP.
          CALLBACK_ mcb = [cb](int code, const std::string& content) {
            using Reply = std::function<void(
                // Response callback alias.
                const drogon::HttpResponsePtr&)>;
            // Map status to reply.
            switch (code) {
              // Success.
              case api::v2::Http::Status::OK:
                Helper::successResponse(
                    // Send 200.
                    content,
                    Reply(cb));
                break;  // Leave the switch.
              case api::v2::Http::Status::INTERNAL_SERVER_ERROR:  // Failure.
                Helper::failureResponse(content,
                                        Reply(cb));  // Send tagged 500.
                break;                               // Leave the switch.
              default:                               // Other error.
                Helper::errorResponse(static_cast<drogon::HttpStatusCode>(code),
                                      content,
                                      Reply(cb));  // Reuse numeric status.
                break;                             // Leave the switch.
            }
          };

          Requester requester(req->getAttributes()->get<uint64_t>("clientID"),
                              req->getAttributes()->get<std::string>(
                                  "clientGroup"));  // Caller identity for
                                                    // permissions.

          auto ctlIt =
              clientRegistry.find(clientType);  // Look up controller for type.
          // Unknown type.
          if (ctlIt == clientRegistry.end()) {
            std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                           fmt::format("Type mapping not found for: {}",
                                       clientType));  // Reply 400.
            co_return;                                // Stop.
          }

          // Guard the call.
          try {
            co_await std::visit(
                [&](auto& controller)
                    -> drogon::Task<void> {  // Dispatch to right controller.
                  // Delete asynchronously by id.
                  // Chosen variant.
                  // Unexpected error.
                  co_await controller->DeleteAsync(std::move(mcb),
                                                   std::move(requester),
                                                   id);
                },
                ctlIt->second);
          } catch (const std::exception& e) {
            Helper::failureResponse(e.what(), std::move(cb));  // Reply 500.
          }
        });

        return;  // Coroutine handles rest.
      }

      executeControllerMethod(*dbExecutor,
                              clientRegistry,
                              clientType,
                              &ClientControllerBase::Delete,
                              req,
                              std::move(callback),
                              *id);  // Blocking mode: dispatch Delete by id.
    }
    /**
     * @brief Search clients using the criteria in the JSON request body.
     * @details POST /{clientType}/search (SECURE). Async path co_awaits
     * SearchAsync; the sync path uses executeControllerMethod. Unknown
     * clientType yields 400.
     * @param[in] req Request carrying the body and Auth identity
     * attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] clientType Registry key.
     */
    // HTTP handler that searches clients.
    void search(const drogon::HttpRequestPtr&                         req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                const std::string& clientType) {
      const bool async = asyncDb_;  // Which database mode is configured.
      // Async coroutine path.
      if (async) {
        auto cb = std::move(callback);  // Move callback into coroutine.
        // Launch coroutine with captured
        // state.
        drogon::async_run([this, req, cb, clientType]() mutable
                              -> drogon::Task<void> {
          // Adapt status to HTTP.
          CALLBACK_ mcb = [cb](int code, const std::string& content) {
            using Reply = std::function<void(
                // Response callback alias.
                const drogon::HttpResponsePtr&)>;
            // Map status to reply.
            switch (code) {
              // Success.
              case api::v2::Http::Status::OK:
                Helper::successResponse(
                    // Send 200.
                    content,
                    Reply(cb));
                break;  // Leave the switch.
              case api::v2::Http::Status::INTERNAL_SERVER_ERROR:  // Failure.
                Helper::failureResponse(content,
                                        Reply(cb));  // Send tagged 500.
                break;                               // Leave the switch.
              default:                               // Other error.
                Helper::errorResponse(static_cast<drogon::HttpStatusCode>(code),
                                      content,
                                      Reply(cb));  // Reuse numeric status.
                break;                             // Leave the switch.
            }
          };

          Requester requester(req->getAttributes()->get<uint64_t>("clientID"),
                              req->getAttributes()->get<std::string>(
                                  "clientGroup"));  // Caller identity for
                                                    // permissions.

          auto ctlIt =
              clientRegistry.find(clientType);  // Look up controller for type.
          // Unknown type.
          if (ctlIt == clientRegistry.end()) {
            std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                           fmt::format("Type mapping not found for: {}",
                                       clientType));  // Reply 400.
            co_return;                                // Stop.
          }

          // Guard the call.
          try {
            co_await std::visit(
                [&](auto& controller)
                    -> drogon::Task<void> {  // Dispatch to right
                                             // controller.
                  // Search asynchronously
                  // with criteria in body.
                  // Chosen variant.
                  // Unexpected error.
                  co_await controller->SearchAsync(std::move(mcb),
                                                   std::move(requester),
                                                   req->body());
                },
                ctlIt->second);
          } catch (const std::exception& e) {
            Helper::failureResponse(e.what(), std::move(cb));  // Reply 500.
          }
        });

        return;  // Coroutine handles rest.
      }

      executeControllerMethod(*dbExecutor,
                              clientRegistry,
                              clientType,
                              &ClientControllerBase::Search,
                              req,
                              std::move(callback),
                              req->body());  // Blocking mode: dispatch Search
                                             // with body.
    }
    /**
     * @brief List the services associated with the client identified by
     * "id".
     * @details GET /{clientType}/services (SECURE). A bad id yields 400 and
     * returns early; the async path co_awaits GetServicesAsync, the sync
     * path uses executeControllerMethod.
     * @param[in] req Request carrying the id parameter and Auth identity
     * attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] clientType Registry key.
     */
    // HTTP handler listing a client's services.
    void getservices(
        const drogon::HttpRequestPtr&                         req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string&                                    clientType) {
      // Reject a bad id with 400 before scheduling the async coroutine.
      auto id =
          parseIdParam(req, "id", callback);  // Parse id; 400 sent on failure.
      if (!id)   // No id means error response was sent.
        return;  // Leave early.

      const bool async = asyncDb_;  // Which database mode is configured.
      // Async coroutine path.
      if (async) {
        auto cb = std::move(callback);  // Move callback into coroutine.
        drogon::async_run([this,
                           req,
                           cb,
                           clientType,
                           // Capture id too.
                           id]() mutable -> drogon::Task<void> {
          // Adapt status to HTTP.
          CALLBACK_ mcb = [cb](int code, const std::string& content) {
            using Reply = std::function<void(
                // Response callback alias.
                const drogon::HttpResponsePtr&)>;
            // Map status to reply.
            switch (code) {
              // Success.
              case api::v2::Http::Status::OK:
                Helper::successResponse(
                    // Send 200.
                    content,
                    Reply(cb));
                break;  // Leave the switch.
              case api::v2::Http::Status::INTERNAL_SERVER_ERROR:  // Failure.
                Helper::failureResponse(content,
                                        Reply(cb));  // Send tagged 500.
                break;                               // Leave the switch.
              default:                               // Other error.
                Helper::errorResponse(static_cast<drogon::HttpStatusCode>(code),
                                      content,
                                      Reply(cb));  // Reuse numeric status.
                break;                             // Leave the switch.
            }
          };

          Requester requester(req->getAttributes()->get<uint64_t>("clientID"),
                              req->getAttributes()->get<std::string>(
                                  "clientGroup"));  // Caller identity for
                                                    // permissions.

          auto ctlIt =
              clientRegistry.find(clientType);  // Look up controller for type.
          // Unknown type.
          if (ctlIt == clientRegistry.end()) {
            std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                           fmt::format("Type mapping not found for: {}",
                                       clientType));  // Reply 400.
            co_return;                                // Stop.
          }

          // Guard the call.
          try {
            co_await std::visit(
                [&](auto& controller)
                    -> drogon::Task<void> {  // Dispatch to right controller.
                  // List services
                  // asynchronously by id.
                  // Chosen variant.
                  // Unexpected error.
                  co_await controller->GetServicesAsync(std::move(mcb),
                                                        std::move(requester),
                                                        id);
                },
                ctlIt->second);
          } catch (const std::exception& e) {
            Helper::failureResponse(e.what(), std::move(cb));  // Reply 500.
          }
        });

        return;  // Coroutine handles rest.
      }

      executeControllerMethod(*dbExecutor,
                              clientRegistry,
                              clientType,
                              &ClientControllerBase::GetServices,
                              req,
                              std::move(callback),
                              *id);  // Blocking mode: dispatch GetServices by
                                     // id.
    }

    // create/login are INSECURE; the remaining routes are SECURE (timing +
    // rate limit + auth).
    METHOD_LIST_BEGIN  // Start the Drogon route-registration block.
        METHOD_ADD(Clients::create,
                   "/{clientType}/create",
                   drogon::Post,
                   INSECURE);  // POST create, public.
    METHOD_ADD(Clients::login,
               "/{clientType}/login",
               drogon::Post,
               INSECURE);  // POST login, public.
    METHOD_ADD(Clients::logout,
               "/{clientType}/logout",
               drogon::Post,
               SECURE);  // POST logout, protected.
    METHOD_ADD(Clients::suspend,
               "/{clientType}/suspend",
               drogon::Post,
               SECURE);  // POST suspend, protected.
    METHOD_ADD(Clients::activate,
               "/{clientType}/activate",
               drogon::Post,
               SECURE);  // POST activate, protected.
    METHOD_ADD(Clients::read,
               "/{clientType}/read",
               drogon::Post,
               SECURE);  // POST read, protected.
    METHOD_ADD(Clients::update,
               "/{clientType}/update",
               drogon::Put,
               SECURE);  // PUT update, protected.
    METHOD_ADD(Clients::delete_,
               "/{clientType}/delete",
               drogon::Delete,
               SECURE);  // DELETE, protected.
    METHOD_ADD(Clients::search,
               "/{clientType}/search",
               drogon::Post,
               SECURE);  // POST search, protected.
    METHOD_ADD(Clients::getservices,
               "/{clientType}/services",
               drogon::Get,
               SECURE);  // GET services, protected.
   METHOD_LIST_END       // End the route-registration block.

       private :
       /** Shared pool that runs blocking database work (shared with every
          route). */
       std::shared_ptr<DbExecutor>
           dbExecutor =
               Store::getObject<DbExecutor>();  // runs the blocking
                                                // database work, shared
                                                // with every route
    /**
     * @brief Coroutine implementing the prototype async read for a single
     * client entity type T.
     * @details Parses id/schema from the body (400 on malformed JSON),
     * checks read permission via GateKeeper::canRead (permitted when no
     * GateKeeper is registered), validates the read schema, builds SQL with
     * getSqlReadStatement, then co_awaits executeReadQueryCoro. A non-empty
     * result is a success, an empty result and other failures are 400;
     *          DatabaseBusy maps to 503 and other exceptions to 500.
     * @tparam T Client entity type (User or Provider).
     * @param[in] req Request carrying the JSON body and Auth identity
     * attributes.
     * @param[in,out] cb Response callback, consumed exactly once.
     * @param[in] clientType Registry key used for the controller lookup.
     */
    template <typename T>
    drogon::Task<void> readAsyncImpl(
        const drogon::HttpRequestPtr&                         req,
        std::function<void(const drogon::HttpResponsePtr&)>&& cb,
        const std::string& clientType);  // Declaration; the definition is
                                         // at the bottom of this file.

    /**
     * Used by readAsyncImpl for the CPU-only permission check.
     */
    std::shared_ptr<GateKeeper> gateKeeper_ =
        Store::cachedObject<GateKeeper>();  // Cached validator for the
                                            // CPU-only read permission
                                            // check.
    /**
     * Used by readAsyncImpl to run the asynchronous read query.
     */
    std::shared_ptr<DatabaseController> dbController_ =
        Store::cachedObject<DatabaseController>();  // Cached controller
                                                    // that runs the async
                                                    // SQL query.
    /**
     * Whether the configured DB mode uses async controller coroutines.
     */
    const bool asyncDb_ = Store::cachedObject<Configurator>()
                              ->get<Configurator::DatabaseConfig>()
                              .async;  // Cached flag selecting async
                                       // coroutines or blocking executor.
    /**
     * Concrete client controllers, one per supported client type.
     */
    using ClientVariant = std::variant<
        std::shared_ptr<ClientController<User>>,
        std::shared_ptr<ClientController<Provider>>>;  // Holds one of two
                                                       // shared client
                                                       // controller pointers.

    /**
     * Maps a clientType path key to its controller variant.
     */
    std::unordered_map<std::string_view, ClientVariant> clientRegistry =
        {// Map URL keys to their controllers.
         {"users", Store::getObject<ClientController<User>>()},
         {"providers",
          Store::getObject<ClientController<Provider>>()}};  // "users" and
                                                             // "providers"
                                                             // entries.
  };

  /**
   * @brief Coroutine definition of the prototype async read; see the in-class
   * declaration.
   * @see Clients::readAsyncImpl
   */
  // Out-of-class definition of the templated coroutine.
  template <typename T>
  drogon::Task<void> Clients::readAsyncImpl(
      const drogon::HttpRequestPtr&                         req,
      std::function<void(const drogon::HttpResponsePtr&)>&& cb,
      const std::string&                                    clientType) {
    // Adapter: controller status -> HTTP response.
    CALLBACK_ mcb = [cb](int code, const std::string& content) {
      using Reply = std::function<void(
          const drogon::HttpResponsePtr&)>;  // Response callback alias.
      // Map numeric status to reply.
      switch (code) {
        case api::v2::Http::Status::OK:                 // Success.
          Helper::successResponse(content, Reply(cb));  // Send 200 with body.
          break;                                        // Leave the switch.
        // Failure.
        case api::v2::Http::Status::INTERNAL_SERVER_ERROR:
          Helper::failureResponse(content, Reply(cb));  // Send tagged 500.
          break;                                        // Leave the switch.
        default:                                        // Other error.
          Helper::errorResponse(static_cast<drogon::HttpStatusCode>(code),
                                content,
                                Reply(cb));  // Reuse numeric status.
          break;                             // Leave the switch.
      }
    };

    Requester
        requester(req->getAttributes()->get<uint64_t>("clientID"),
                  req->getAttributes()->get<std::string>(
                      "clientGroup"));  // Caller identity for permissions.

    // Guard every step of the async read.
    try {
      jsoncons::json request_j;  // Parsed JSON body holder.
      uint64_t       _id = 0;    // Id extracted from the body.
      std::unordered_set<std::string>
          schema;  // Set of field names to read, extracted from the body.
      // Parsing the body can fail.
      try {
        request_j = jsoncons::json::parse(
            req->body());  // Parse the request body as JSON.
        _id = request_j.at("id").as<uint64_t>();  // Read the "id" field
                                                  // as a 64-bit integer.
        // Read the "schema"
        // field as a set of
        // column names.
        // Malformed JSON or missing/invalid
        // fields.
        schema = request_j.at("schema").as<std::unordered_set<std::string>>();
      } catch (const std::exception&) {
        std::move(
            mcb)(api::v2::Http::Status::BAD_REQUEST,
                 "Invalid request body: malformed JSON or missing/invalid "
                 "'id'/'schema'.");  // Reply 400 with a helpful message.
        co_return;                   // End coroutine.
      }

      auto ctlIt =
          clientRegistry.find(clientType);  // Find controller for this type.
      // Unknown type.
      if (ctlIt == clientRegistry.end()) {
        std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                       fmt::format("Type mapping not found for: {}",
                                   clientType));  // Reply 400.
        co_return;                                // Stop.
      }

      // Permission check (CPU-only for clients, no DB).
      api::v2::Http::Error
          error;  // Filled with a code/message if permission is denied.
      // Extract controller to get gateKeeper? Use a default-constructed
      // entity controller? We reuse sync permission via GateKeeper
      // global.
      auto gateKeeper = gateKeeper_;  // may not be registered; fallback
      // Simpler: use PermissionManager indirectly not available; skip?
      // Instead keep sync check by constructing controller. For
      // prototype, delegate permission check to the existing sync
      // controller method is not possible async. We'll perform the same
      // assert_group_id_match by calling GateKeeper if registered. If
      // GateKeeper is not in Store, allow (should not happen in
      // production).
      // Coroutine lambda: returns
      // whether reading is allowed.
      auto checkPermission =
          [gateKeeper, requester, _id, &error]() -> drogon::Task<bool> {
        // A GateKeeper is registered in normal
        // operation.
        if (gateKeeper) {
          co_return gateKeeper->canRead<T>(requester,
                                           _id,
                                           error);  // Ask GateKeeper for
                                                    // CPU-only read permission.
        }
        co_return true;  // Fallback: allow when no GateKeeper exists
                         // (not expected in production).
      };
      bool ok = co_await checkPermission();  // Wait for the permission result.
      // Permission denied.
      if (!ok) {
        std::move(mcb)(error.code,
                       error.message);  // Reply with the GateKeeper's error
                                        // code and message.
        co_return;                      // Stop.
      }

      Validator::Rule rule(Validator::Rule::Action::ASSERT_NOT_PRESENT,
                           {"id",
                            "username",
                            "password",
                            "created_at",
                            "updated_at"});  // Forbid sensitive/system columns
                                             // in the requested schema.
      if (!Validator::
              // Check the schema against the
              // "{table}_safe" definition.
          validateDatabaseReadSchema(schema,
                                     std::format("{}_safe", T::getTableName()),
                                     error,
                                     rule)) {
        std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                       fmt::format("Failed to validate request body, {}.",
                                   error.message));  // Reply 400 if the schema
                                                     // is invalid.
        co_return;                                   // Stop.
      }

      T entity((Read_t(schema,
                       _id)));  // Build the entity holding the requested schema
                                // and id.

      // Build SQL
      pqxx::params params;  // Bind parameters for the SQL statement
                            // (prevents injection).
      auto         query = entity.getSqlReadStatement(
          params);  // Ask the entity for its parameterized SELECT.
      // The entity could not build a statement.
      if (!query.has_value()) {
        std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                       fmt::format("Failed to get SQL statement for {}.",
                                   entity.getTableName()));  // Reply 400 with
                                                             // the table name.
        co_return;                                           // Stop.
      }

      auto dbController = dbController_;  // Copy the shared controller
                                          // pointer for the coroutine.
      auto result =
          co_await dbController
              ->executeReadQueryCoro(query.value(),
                                     params);  // Suspend until the async SQL
                                               // query finishes.
      // A row was returned.
      if (result.has_value() && !result->empty()) {
        std::move(mcb)(api::v2::Http::Status::OK,
                       result.value()
                           .template as<std::string>());  // Reply 200 with the
                                                          // row as JSON text.
        co_return;                                        // Stop.
      }
      // Query succeeded but found no rows.
      if (result.has_value()) {
        std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                       "Query returned empty result, please recheck your "
                       "parameters.");  // Reply 400 advising the caller.
        co_return;                      // Stop.
      }
      // Query building failed:
      // reply 400.
      // Connection pool exhausted.
      std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                     "Failed to create sql query");
    } catch (const DatabaseBusy& e) {
      // Reply retryable 503.
      // Any other failure.
      Helper::errorResponse(drogon::k503ServiceUnavailable,
                            e.what(),
                            std::move(cb));
    } catch (const std::exception& e) {
      // Reply 500.
      Helper::failureResponse(e.what(), std::move(cb));
    }

    co_return;  // Finish the coroutine.
  }

}  // namespace api::v2
