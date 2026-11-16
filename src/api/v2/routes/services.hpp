/**
 * @file services.hpp
 * @brief REST controller for service-directory entities (clinics, pharmacies,
 * laboratories, centers).
 * @details This file defines the Services controller, which manages the
 * directory of service providers through create, read, update, delete and
 * search endpoints. The {serviceType} part of the URL (clinics, pharmacies,
 * laboratories or radiologycenters) selects one concrete ServiceController from
 * a std::variant registry. Depending on the configuration each handler either
 * awaits an async controller coroutine or uses executeControllerMethod for
 * blocking database work. Handlers translate the controller's numeric result
 * into a standard JSON HTTP response and build a Requester from the identity
 * attributes inserted by Auth.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This controller is the HTTP front door for the service directory. Drogon
 * routes the /api/v2 service URLs here; the handlers call the shared
 * dispatcher, which in turn calls the service controllers and entities and
 * finally the database. It relies on Auth for caller identity and on the
 * service locator for the executor and controllers. Without it clients could
 * not add, browse, edit or search the clinics, pharmacies, labs and centers
 * listed in the system.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - REST create/read/update/delete (CRUD) routes:
 * https://developer.mozilla.org/en-US/docs/Web/HTTP/Methods
 * - std::variant (a type-safe union):
 * https://en.cppreference.com/w/cpp/utility/variant
 * - HTTP filters/middleware: https://drogon.docsforge.com/master/filter/
 * - CORS: https://developer.mozilla.org/en-US/docs/Web/HTTP/CORS
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Only process this header once per source file.
// Drogon controller base and the request/response/status types used by the
// handlers.
// Base class drogon::HttpController used by Services.
#include <drogon/HttpController.h>
// Request type carrying body, attributes and URL parameters.
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>  // Response type built by the JSON helpers.
#include <drogon/HttpTypes.h>     // HTTP verb and status constants.

#include <functional>  // std::function is the type of the response callback.
#include <memory>  // std::shared_ptr for the shared executor and controllers.
#include <string>  // std::string for URLs, keys and messages.
#include <string_view>    // std::string_view keys the registry map cheaply.
#include <unordered_map>  // The registry maps type names to controllers.
#include <utility>        // std::move for consuming callbacks.
// std::variant holds one of several controller pointer types.
#include <variant>

// Shared dispatch helpers, concrete controllers and entity types for each
// service flavour. The next line is marked for IWYU so the include is not
// removed: this header needs the shared helpers.
#include "api/v2/basic/common.hpp"  // IWYU pragma: keep
// ServiceController<T> template definition.
#include "controllers/servicecontroller/servicecontroller.hpp"
// Base class exposing the shared method pointers.
#include "controllers/servicecontroller/servicecontrollerbase.hpp"
// DbExecutor runs blocking database work off the IO thread.
#include "database/dbexecutor.hpp"
// drogon::async_run and drogon::Task for the async path.
#include "drogon/utils/coroutine.h"
#include "entities/services/clinics/clinics.hpp"  // Clinics entity type.
#include "entities/services/laboratories.hpp"     // Laboratories entity type.
#include "entities/services/pharmacies.hpp"       // Pharmacies entity type.
// RadiologyCenters entity type.
#include "entities/services/radiologycenters.hpp"
#include "store/store.hpp"  // Store/service locator supplies shared objects.

namespace api::v2  // The v2 API controllers live here.
{
  /**
   * @class Services
   * @brief REST controller for service-directory entities (clinics,
   * pharmacies, laboratories, centers).
   * @details A {serviceType} path segment selects a ServiceController<T> from
   * a variant registry. Handlers use the shared callback pattern to map
   * controller status codes to JSON HTTP responses, build a Requester from
   * Auth attributes, and run async coroutines or the DbExecutor-backed
   * dispatch according to DatabaseConfig::async. All routes are SECURE.
   */
  class Services
      : public drogon::HttpController<Services>  // Inherit Drogon's controller
                                                 // template so Drogon registers
                                                 // these routes.
  {
   public:
    /**
     * @brief Create a service entity from the JSON request body.
     * @details POST /{serviceType}/create (SECURE). Async path co_awaits
     * CreateAsync; sync path uses executeControllerMethod. An unknown
     * serviceType yields 400.
     * @param[in] req Request carrying the body and Auth identity
     * attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] serviceType Registry key taken from the path.
     */
    void Create(
        const drogon::HttpRequestPtr&                         req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& serviceType)  // HTTP handler: creates a service
                                         // from the request body.
    {
      const bool async =
          asyncDb_;  // Copy the cached flag selecting the database mode.
      if (async)     // Async mode: run the controller coroutine.
      {
        auto cb = std::move(callback);  // Move the callback into the coroutine.
        drogon::async_run(
            [this, req, cb, serviceType]() mutable
                -> drogon::Task<void>  // Start a coroutine capturing
                                       // its state by value.
            {
              CALLBACK_ mcb = [cb](int code,
                                   const std::string&
                                       content)  // Adapter: controller status
                                                 // -> HTTP response.
              {
                using Reply = std::function<void(
                    // Response callback alias.
                    const drogon::HttpResponsePtr&)>;
                switch (code)  // Map the numeric status to a reply.
                {
                  // Success.
                  case api::v2::Http::Status::OK:
                    Helper::successResponse(content,
                                            Reply(cb));  // Send 200 with the
                                                         // body.
                    break;                               // Leave the switch.
                  case api::v2::Http::Status::
                      INTERNAL_SERVER_ERROR:  // Failure.
                    Helper::failureResponse(content,
                                            Reply(cb));  // Send tagged 500.
                    break;                               // Leave the switch.
                  default:                               // Other error.
                    Helper::errorResponse(static_cast<drogon::HttpStatusCode>(
                                              code),
                                          content,
                                          Reply(cb));  // Reuse the numeric
                                                       // status.
                    break;                             // Leave the switch.
                }
              };

              Requester
                  requester(req->getAttributes()->get<uint64_t>("clientID"),
                            req->getAttributes()->get<std::string>(
                                "clientGroup"));  // Build caller identity for
                                                  // permission checks.

              auto ctlIt =
                  serviceRegistry.find(serviceType);  // Find the controller for
                                                      // this URL type.
              // Unknown type.
              if (ctlIt == serviceRegistry.end())
              {
                std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                               fmt::format("Type mapping not found for: {}",
                                           serviceType));  // Reply 400.
                co_return;                                 // End the coroutine.
              }

              try  // Guard the controller call.
              {
                co_await std::visit(
                    [&](auto& controller) -> drogon::Task<void>
                    {  // Dispatch to the correct controller type.
                      co_await controller
                          ->CreateAsync(std::move(mcb),
                                        std::move(requester),
                                        req->body());  // Create the service
                                                       // asynchronously from
                                                       // the body.
                    },
                    ctlIt->second);  // The selected variant.
              }
              catch (const std::exception& e)  // Unexpected error.
              {
                Helper::failureResponse(e.what(), std::move(cb));  // Reply 500.
              }
            });

        return;  // Coroutine owns the rest.
      }

      executeControllerMethod(*dbExecutor,
                              serviceRegistry,
                              serviceType,
                              &ServiceControllerBase::Create,
                              req,
                              std::move(callback),
                              req->body());  // Blocking mode: dispatch Create
                                             // with the body.
    }

    /**
     * @brief Read service entities using the criteria in the JSON request
     * body.
     * @details POST /{serviceType}/read (SECURE). Async path co_awaits
     * ReadAsync; sync path uses executeControllerMethod. An unknown
     * serviceType yields 400.
     * @param[in] req Request carrying the body and Auth identity
     * attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] serviceType Registry key taken from the path.
     */
    void Read(
        const drogon::HttpRequestPtr&                         req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& serviceType)  // HTTP handler for reading services.
    {
      const bool async = asyncDb_;  // Which database mode is configured.
      if (async)                    // Async coroutine path.
      {
        auto cb = std::move(callback);  // Move callback into the coroutine.
        drogon::async_run(
            [this, req, cb, serviceType]() mutable
                -> drogon::Task<void>  // Launch coroutine with captured
                                       // state.
            {
              CALLBACK_ mcb =
                  [cb](int                code,
                       const std::string& content)  // Adapt status to HTTP.
              {
                using Reply = std::function<void(
                    // Response callback alias.
                    const drogon::HttpResponsePtr&)>;
                switch (code)  // Map status to reply.
                {
                  // Success.
                  case api::v2::Http::Status::OK:
                    Helper::successResponse(
                        // Send 200.
                        content,
                        Reply(cb));
                    break;  // Leave the switch.
                  case api::v2::Http::Status::
                      INTERNAL_SERVER_ERROR:  // Failure.
                    Helper::failureResponse(content,
                                            Reply(cb));  // Send tagged 500.
                    break;                               // Leave the switch.
                  default:                               // Other error.
                    Helper::errorResponse(static_cast<drogon::HttpStatusCode>(
                                              code),
                                          content,
                                          Reply(cb));  // Reuse numeric status.
                    break;                             // Leave the switch.
                }
              };

              Requester requester(req->getAttributes()->get<uint64_t>(
                                      "clientID"),
                                  req->getAttributes()->get<std::string>(
                                      "clientGroup"));  // Caller identity for
                                                        // permissions.

              auto ctlIt = serviceRegistry.find(
                  serviceType);  // Look up controller for this type.
              // Unknown type.
              if (ctlIt == serviceRegistry.end())
              {
                std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                               fmt::format("Type mapping not found for: {}",
                                           serviceType));  // Reply 400.
                co_return;                                 // Stop.
              }

              try  // Guard the call.
              {
                co_await std::visit(
                    [&](auto& controller) -> drogon::Task<void>
                    {  // Dispatch to the right controller.
                      co_await controller
                          ->ReadAsync(std::move(mcb),
                                      std::move(requester),
                                      req->body());  // Read asynchronously
                                                     // with criteria in body.
                    },
                    ctlIt->second);  // Chosen variant.
              }
              catch (const std::exception& e)  // Unexpected error.
              {
                Helper::failureResponse(e.what(), std::move(cb));  // Reply 500.
              }
            });

        return;  // Coroutine handles the rest.
      }

      executeControllerMethod(*dbExecutor,
                              serviceRegistry,
                              serviceType,
                              &ServiceControllerBase::Read,
                              req,
                              std::move(callback),
                              req->body());  // Blocking mode: dispatch Read
                                             // with the body.
    }

    /**
     * @brief Update the service entity identified by the "id" query
     * parameter.
     * @details PUT /{serviceType}/update (SECURE). A bad id yields 400 and
     * returns early; async path co_awaits UpdateAsync with the parsed id,
     * sync path uses executeControllerMethod.
     * @param[in] req Request carrying the body, id parameter and Auth
     * identity attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] serviceType Registry key taken from the path.
     */
    void Update(
        const drogon::HttpRequestPtr&                         req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& serviceType)  // HTTP handler for updating a service.
    {
      // Reject a bad id with 400 before scheduling the async coroutine.
      auto id = parseIdParam(req,
                             "id",
                             callback);  // Parse id; callback already answered
                                         // 400 on failure.
      if (!id)   // No id means an error response was sent.
        return;  // Leave early to avoid a second response.

      const bool async = asyncDb_;  // Which database mode is configured.
      if (async)                    // Async coroutine path.
      {
        auto cb = std::move(callback);  // Move callback into the coroutine.
        drogon::async_run(
            [this, req, cb, serviceType, id]() mutable
                -> drogon::Task<void>  // Capture the parsed id too.
            {
              CALLBACK_ mcb =
                  [cb](int                code,
                       const std::string& content)  // Adapt status to HTTP.
              {
                using Reply = std::function<void(
                    // Response callback alias.
                    const drogon::HttpResponsePtr&)>;
                switch (code)  // Map status to reply.
                {
                  // Success.
                  case api::v2::Http::Status::OK:
                    Helper::successResponse(
                        // Send 200.
                        content,
                        Reply(cb));
                    break;  // Leave the switch.
                  case api::v2::Http::Status::
                      INTERNAL_SERVER_ERROR:  // Failure.
                    Helper::failureResponse(content,
                                            Reply(cb));  // Send tagged 500.
                    break;                               // Leave the switch.
                  default:                               // Other error.
                    Helper::errorResponse(static_cast<drogon::HttpStatusCode>(
                                              code),
                                          content,
                                          Reply(cb));  // Reuse numeric status.
                    break;                             // Leave the switch.
                }
              };

              Requester requester(req->getAttributes()->get<uint64_t>(
                                      "clientID"),
                                  req->getAttributes()->get<std::string>(
                                      "clientGroup"));  // Caller identity for
                                                        // permissions.

              auto ctlIt = serviceRegistry.find(
                  serviceType);  // Look up controller for this type.
              // Unknown type.
              if (ctlIt == serviceRegistry.end())
              {
                std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                               fmt::format("Type mapping not found for: {}",
                                           serviceType));  // Reply 400.
                co_return;                                 // Stop.
              }

              try  // Guard the call.
              {
                co_await std::visit(
                    [&](auto& controller) -> drogon::Task<void>
                    {  // Dispatch to the right controller.
                      co_await controller
                          ->UpdateAsync(std::move(mcb),
                                        std::move(requester),
                                        req->body(),
                                        id);  // Update asynchronously with body
                                              // and id.
                    },
                    ctlIt->second);  // Chosen variant.
              }
              catch (const std::exception& e)  // Unexpected error.
              {
                Helper::failureResponse(e.what(), std::move(cb));  // Reply 500.
              }
            });

        return;  // Coroutine handles the rest.
      }

      executeControllerMethod(*dbExecutor,
                              serviceRegistry,
                              serviceType,
                              &ServiceControllerBase::Update,
                              req,
                              std::move(callback),
                              req->body(),
                              *id);  // Blocking mode: dispatch Update with body
                                     // and id.
    }
    /**
     * @brief Delete the service entity identified by the "id" query
     * parameter.
     * @details DELETE /{serviceType}/delete (SECURE). A bad id yields 400
     * and returns early; async path co_awaits DeleteAsync, sync path uses
     * executeControllerMethod.
     * @param[in] req Request carrying the id parameter and Auth identity
     * attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] serviceType Registry key taken from the path.
     */
    void Delete(
        const drogon::HttpRequestPtr&                         req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& serviceType)  // HTTP handler for deleting a service.
    {
      // Reject a bad id with 400 before scheduling the async coroutine.
      auto id =
          parseIdParam(req,
                       "id",
                       callback);  // Parse id; 400 already sent on failure.
      if (!id)                     // No id means an error response was sent.
        return;                    // Leave early.

      const bool async = asyncDb_;  // Which database mode is configured.
      if (async)                    // Async coroutine path.
      {
        auto cb = std::move(callback);  // Move callback into the coroutine.
        drogon::async_run(
            [this,
             req,
             cb,
             serviceType,
             id]() mutable -> drogon::Task<void>  // Capture the parsed id.
            {
              CALLBACK_ mcb =
                  [cb](int                code,
                       const std::string& content)  // Adapt status to HTTP.
              {
                using Reply = std::function<void(
                    // Response callback alias.
                    const drogon::HttpResponsePtr&)>;
                switch (code)  // Map status to reply.
                {
                  // Success.
                  case api::v2::Http::Status::OK:
                    Helper::successResponse(
                        // Send 200.
                        content,
                        Reply(cb));
                    break;  // Leave the switch.
                  case api::v2::Http::Status::
                      INTERNAL_SERVER_ERROR:  // Failure.
                    Helper::failureResponse(content,
                                            Reply(cb));  // Send tagged 500.
                    break;                               // Leave the switch.
                  default:                               // Other error.
                    Helper::errorResponse(static_cast<drogon::HttpStatusCode>(
                                              code),
                                          content,
                                          Reply(cb));  // Reuse numeric status.
                    break;                             // Leave the switch.
                }
              };

              Requester requester(req->getAttributes()->get<uint64_t>(
                                      "clientID"),
                                  req->getAttributes()->get<std::string>(
                                      "clientGroup"));  // Caller identity for
                                                        // permissions.

              auto ctlIt = serviceRegistry.find(
                  serviceType);  // Look up controller for this type.
              // Unknown type.
              if (ctlIt == serviceRegistry.end())
              {
                std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                               fmt::format("Type mapping not found for: {}",
                                           serviceType));  // Reply 400.
                co_return;                                 // Stop.
              }

              try  // Guard the call.
              {
                co_await std::visit(
                    [&](auto& controller) -> drogon::Task<void>
                    {  // Dispatch to the right controller.
                      co_await controller
                          ->DeleteAsync(std::move(mcb),
                                        std::move(requester),
                                        id);  // Delete asynchronously by id.
                    },
                    ctlIt->second);  // Chosen variant.
              }
              catch (const std::exception& e)  // Unexpected error.
              {
                Helper::failureResponse(e.what(), std::move(cb));  // Reply 500.
              }
            });

        return;  // Coroutine handles the rest.
      }

      executeControllerMethod(*dbExecutor,
                              serviceRegistry,
                              serviceType,
                              &ServiceControllerBase::Delete,
                              req,
                              std::move(callback),
                              *id);  // Blocking mode: dispatch Delete by id.
    }
    /**
     * @brief Search service entities using the criteria in the JSON request
     * body.
     * @details POST /{serviceType}/search (SECURE). Async path co_awaits
     * SearchAsync; sync path uses executeControllerMethod. An unknown
     * serviceType yields 400.
     * @param[in] req Request carrying the body and Auth identity
     * attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] serviceType Registry key taken from the path.
     */
    void Search(
        const drogon::HttpRequestPtr&                         req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& serviceType)  // HTTP handler for searching services.
    {
      const bool async = asyncDb_;  // Which database mode is configured.
      if (async)                    // Async coroutine path.
      {
        auto cb = std::move(callback);  // Move callback into the coroutine.
        drogon::async_run(
            [this, req, cb, serviceType]() mutable
                -> drogon::Task<void>  // Launch coroutine with captured
                                       // state.
            {
              CALLBACK_ mcb =
                  [cb](int                code,
                       const std::string& content)  // Adapt status to HTTP.
              {
                using Reply = std::function<void(
                    // Response callback alias.
                    const drogon::HttpResponsePtr&)>;
                switch (code)  // Map status to reply.
                {
                  // Success.
                  case api::v2::Http::Status::OK:
                    Helper::successResponse(
                        // Send 200.
                        content,
                        Reply(cb));
                    break;  // Leave the switch.
                  case api::v2::Http::Status::
                      INTERNAL_SERVER_ERROR:  // Failure.
                    Helper::failureResponse(content,
                                            Reply(cb));  // Send tagged 500.
                    break;                               // Leave the switch.
                  default:                               // Other error.
                    Helper::errorResponse(static_cast<drogon::HttpStatusCode>(
                                              code),
                                          content,
                                          Reply(cb));  // Reuse numeric status.
                    break;                             // Leave the switch.
                }
              };

              Requester requester(req->getAttributes()->get<uint64_t>(
                                      "clientID"),
                                  req->getAttributes()->get<std::string>(
                                      "clientGroup"));  // Caller identity for
                                                        // permissions.

              auto ctlIt = serviceRegistry.find(
                  serviceType);  // Look up controller for this type.
              // Unknown type.
              if (ctlIt == serviceRegistry.end())
              {
                std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                               fmt::format("Type mapping not found for: {}",
                                           serviceType));  // Reply 400.
                co_return;                                 // Stop.
              }

              try  // Guard the call.
              {
                co_await std::visit(
                    [&](auto& controller) -> drogon::Task<void>
                    {  // Dispatch to the right controller.
                      co_await controller
                          ->SearchAsync(std::move(mcb),
                                        std::move(requester),
                                        req->body());  // Search asynchronously
                                                       // with criteria in body.
                    },
                    ctlIt->second);  // Chosen variant.
              }
              catch (const std::exception& e)  // Unexpected error.
              {
                Helper::failureResponse(e.what(), std::move(cb));  // Reply 500.
              }
            });

        return;  // Coroutine handles the rest.
      }

      executeControllerMethod(*dbExecutor,
                              serviceRegistry,
                              serviceType,
                              &ServiceControllerBase::Search,
                              req,
                              std::move(callback),
                              req->body());  // Blocking mode: dispatch Search
                                             // with the body.
    }
    // All routes are SECURE (rate limit + timing + token auth).
    METHOD_LIST_BEGIN  // Start the Drogon route-registration block.
        METHOD_ADD(Services::Create,
                   "/{serviceType}/create",
                   drogon::Post,
                   SECURE);  // POST create for a {serviceType}.
    METHOD_ADD(Services::Read,
               "/{serviceType}/read",
               drogon::Post,
               SECURE);  // POST read for a {serviceType}.
    METHOD_ADD(Services::Update,
               "/{serviceType}/update",
               drogon::Put,
               SECURE);  // PUT update for a {serviceType}.
    METHOD_ADD(Services::Delete,
               "/{serviceType}/delete",
               drogon::Delete,
               SECURE);  // DELETE for a {serviceType}.
    METHOD_ADD(Services::Search,
               "/{serviceType}/search",
               drogon::HttpMethod::Post,
               SECURE);  // POST search for a {serviceType}.
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
     * Whether the configured DB mode uses async controller coroutines.
     */
    const bool asyncDb_ = Store::cachedObject<Configurator>()
                              ->get<Configurator::DatabaseConfig>()
                              .async;  // Cached flag selecting async
                                       // coroutines or blocking executor.
    /**
     * Concrete service controllers, one per supported serviceType.
     */
    using ServiceVariant = std::variant<
        std::shared_ptr<ServiceController<Clinics>>,
        std::shared_ptr<ServiceController<Pharmacies>>,  // Holds one of four
                                                         // shared service
                                                         // controller pointers.
        std::shared_ptr<ServiceController<Laboratories>>,
        std::shared_ptr<ServiceController<
            RadiologyCenters>>>;  // Laboratory and radiology-center
                                  // controllers.

    /**
     * Maps a serviceType path key to its controller variant.
     */
    std::unordered_map<std::string_view, ServiceVariant> serviceRegistry =
        {{"clinics",
          Store::getObject<ServiceController<Clinics>>()},  // Map URL keys to
                                                            // their
                                                            // controllers.
         {"pharmacies", Store::getObject<ServiceController<Pharmacies>>()},
         {"laboratories",
          Store::getObject<ServiceController<
              Laboratories>>()},  // pharmacies and laboratories entries.
         {"radiologycenters",
          Store::getObject<ServiceController<
              RadiologyCenters>>()}};  // radiologycenters entry closes
                                       // the map.
  };
}  // namespace api::v2
