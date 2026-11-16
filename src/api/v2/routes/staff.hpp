/**
 * @file staff.hpp
 * @brief REST controller for managing staff membership of service entities.
 * @details This file defines the Staff controller, which invites, adds and
 * removes staff members of a service entity. The {serviceType} part of the URL
 * (clinics, pharmacies, laboratories or radiologycenters) selects one concrete
 * StaffController from a std::variant registry. Each handler translates the
 * controller's numeric status into a standard JSON HTTP response and builds a
 * Requester from the identity attributes set by Auth. Depending on the
 * configuration it either awaits an async controller coroutine or uses
 * executeControllerMethod for blocking database work. All routes are SECURE.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This controller is the HTTP front door for managing who works at each service
 * provider. Drogon routes the /api/v2 staff URLs here; the handlers call the
 * shared dispatcher, which reaches the staff controllers and the database. It
 * depends on the Auth filter for identity and on the service locator for the
 * executor and controllers. Without it there would be no API way to invite or
 * change the staff of a clinic, pharmacy, laboratory or radiology center.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - REST routes and HTTP POST:
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
// Drogon controller base and coroutine support; request/response/status types
// used by handlers.
// Base class drogon::HttpController used by Staff.
#include <drogon/HttpController.h>
// Request type carrying body, attributes and URL parameters.
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>  // Response type built by the JSON helpers.
#include <drogon/HttpTypes.h>     // HTTP verb and status constants.
// drogon::async_run and drogon::Task for the async path.
#include <drogon/utils/coroutine.h>

#include <functional>  // std::function is the type of the response callback.
#include <memory>  // std::shared_ptr for the shared executor and controllers.
#include <string>  // std::string for URLs, keys and messages.
#include <string_view>    // std::string_view keys the registry map cheaply.
#include <unordered_map>  // The registry maps type names to controllers.
#include <utility>        // std::move for consuming callbacks.
// std::variant holds one of several controller pointer types.
#include <variant>

// Shared dispatch helpers, configuration, concrete controllers and service
// entity types. The next line is marked for IWYU so the include is not removed:
// this header needs the shared helpers.
#include "api/v2/basic/common.hpp"  // IWYU pragma: keep
// Reads DatabaseConfig::async to choose async or blocking mode.
#include "configurator/configurator.hpp"
// StaffController<T> template definition.
#include "controllers/staffcontroller/staffcontroller.hpp"
// Base class exposing the shared method pointers.
#include "controllers/staffcontroller/staffcontrollerBase.hpp"
// DbExecutor runs blocking database work off the IO thread.
#include "database/dbexecutor.hpp"
#include "entities/services/clinics/clinics.hpp"  // Clinics entity type.
#include "entities/services/laboratories.hpp"     // Laboratories entity type.
#include "entities/services/pharmacies.hpp"       // Pharmacies entity type.
// RadiologyCenters entity type.
#include "entities/services/radiologycenters.hpp"
#include "store/store.hpp"  // Store/service locator supplies shared objects.

// The v2 API controllers live here.
namespace api::v2 {
  /**
   * @class Staff
   * @brief REST controller for managing staff membership of service entities.
   * @details A {serviceType} path segment selects a StaffController<T> from a
   * variant registry (clinics, pharmacies, laboratories, radiologycenters).
   * Handlers use the shared callback pattern to map controller status codes
   * to JSON HTTP responses, build a Requester from Auth attributes, and run
   * async coroutines or the DbExecutor-backed dispatch according to
   *          DatabaseConfig::async. All routes are SECURE. Controllers and
   * the executor are shared service-locator objects.
   * @note Drogon prepends its controller path (/api/v2/staff) to the
   * METHOD_ADD paths below, so the effective URLs contain a doubled "staff"
   * segment (e.g. /api/v2/staff/.../staff/invite).
   */
  class Staff
      // Inherit Drogon's controller
      // template so Drogon registers
      // these routes.
      : public drogon::HttpController<Staff> {
   public:
    /**
     * @brief Invite a staff member to the service entity described by the
     * JSON body.
     * @details POST /services/{serviceType}/staff/invite (SECURE). Async
     * path co_awaits InviteStaffToEntityAsync; sync path uses
     * executeControllerMethod with InviteStaffToEntity. An unknown
     * serviceType yields 400.
     * @param[in] req Request carrying the body and Auth identity
     * attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] serviceType Registry key taken from the path.
     */
    // HTTP handler that invites a staff member.
    void Invite(const drogon::HttpRequestPtr&                         req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                const std::string& serviceType) {
      const bool async =
          asyncDb_;  // Copy cached flag selecting the database mode.
      // Async mode: run the controller coroutine.
      if (async) {
        auto cb = std::move(callback);  // Move callback into the coroutine.
        // Start coroutine capturing its
        // state by value.
        drogon::async_run([this, req, cb, serviceType]() mutable
                              -> drogon::Task<void> {
          // Adapter: controller status
          // -> HTTP response.
          CALLBACK_ mcb = [cb](int code, const std::string& content) {
            using Reply = std::function<void(
                // Response callback alias.
                const drogon::HttpResponsePtr&)>;
            // Map the numeric status to a reply.
            switch (code) {
              // Success.
              case api::v2::Http::Status::OK:
                Helper::successResponse(content,
                                        Reply(cb));  // Send 200 with the
                                                     // body.
                break;                               // Leave the switch.
              case api::v2::Http::Status::INTERNAL_SERVER_ERROR:  // Failure.
                Helper::failureResponse(content,
                                        Reply(cb));  // Send tagged 500.
                break;                               // Leave the switch.
              default:                               // Other error.
                Helper::errorResponse(static_cast<drogon::HttpStatusCode>(code),
                                      content,
                                      Reply(cb));  // Reuse the numeric
                                                   // status.
                break;                             // Leave the switch.
            }
          };

          Requester requester(req->getAttributes()->get<uint64_t>("clientID"),
                              req->getAttributes()->get<std::string>(
                                  "clientGroup"));  // Build caller identity for
                                                    // permission checks.

          auto ctlIt = staffRegistry.find(
              serviceType);  // Find controller for this URL type.
          // Unknown type.
          if (ctlIt == staffRegistry.end()) {
            std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                           fmt::format("Type mapping not found for: {}",
                                       serviceType));  // Reply 400.
            co_return;                                 // End coroutine.
          }

          // Guard the controller call.
          try {
            co_await std::visit(
                [&](auto& controller)
                    -> drogon::Task<void> {  // Dispatch to correct
                                             // controller type.
                  // Invite staff
                  // asynchronously
                  // with body.
                  // The selected variant.
                  // Unexpected error.
                  co_await controller->InviteStaffToEntityAsync(std::move(mcb),
                                                                std::move(
                                                                    requester),
                                                                req->body());
                },
                ctlIt->second);
          } catch (const std::exception& e) {
            Helper::failureResponse(e.what(), std::move(cb));  // Reply 500.
          }
        });

        return;  // Coroutine owns the rest.
      }

      executeControllerMethod(*dbExecutor,
                              staffRegistry,
                              serviceType,
                              &StaffControllerBase::InviteStaffToEntity,
                              req,
                              std::move(callback),
                              req->body());  // Blocking mode: dispatch
                                             // InviteStaffToEntity with body.
    }

    /**
     * @brief Add a staff member to the service entity described by the JSON
     * body.
     * @details POST /services/{serviceType}/staff/add (SECURE). Async path
     * co_awaits AddStaffToEntityAsync; sync path uses
     * executeControllerMethod with AddStaffToEntity. An unknown serviceType
     * yields 400.
     * @param[in] req Request carrying the body and Auth identity
     * attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] serviceType Registry key taken from the path.
     */
    // HTTP handler that adds a staff member.
    void Add(const drogon::HttpRequestPtr&                         req,
             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
             const std::string& serviceType) {
      const bool async = asyncDb_;  // Which database mode is configured.
      // Async coroutine path.
      if (async) {
        auto cb = std::move(callback);  // Move callback into coroutine.
        // Launch coroutine with captured
        // state.
        drogon::async_run([this, req, cb, serviceType]() mutable
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
              staffRegistry.find(serviceType);  // Look up controller for type.
          // Unknown type.
          if (ctlIt == staffRegistry.end()) {
            std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                           fmt::format("Type mapping not found for: {}",
                                       serviceType));  // Reply 400.
            co_return;                                 // Stop.
          }

          // Guard the call.
          try {
            co_await std::visit(
                [&](auto& controller)
                    -> drogon::Task<void> {  // Dispatch to right
                                             // controller.
                  // Add staff
                  // asynchronously
                  // with body.
                  // Chosen variant.
                  // Unexpected error.
                  co_await controller->AddStaffToEntityAsync(std::move(mcb),
                                                             std::move(
                                                                 requester),
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
                              staffRegistry,
                              serviceType,
                              &StaffControllerBase::AddStaffToEntity,
                              req,
                              std::move(callback),
                              req->body());  // Blocking mode: dispatch
                                             // AddStaffToEntity with body.
    }

    /**
     * @brief Remove a staff member from the service entity described by the
     * JSON body.
     * @details POST /services/{serviceType}/staff/remove (SECURE). Async
     * path co_awaits RemoveStaffFromEntityAsync; sync path uses
     * executeControllerMethod with RemoveStaffFromEntity. An unknown
     * serviceType yields 400.
     * @param[in] req Request carrying the body and Auth identity
     * attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] serviceType Registry key taken from the path.
     */
    // HTTP handler that removes a staff member.
    void Remove(const drogon::HttpRequestPtr&                         req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                const std::string& serviceType) {
      const bool async = asyncDb_;  // Which database mode is configured.
      // Async coroutine path.
      if (async) {
        auto cb = std::move(callback);  // Move callback into coroutine.
        // Launch coroutine with captured
        // state.
        drogon::async_run([this, req, cb, serviceType]() mutable
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
              staffRegistry.find(serviceType);  // Look up controller for type.
          // Unknown type.
          if (ctlIt == staffRegistry.end()) {
            std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                           fmt::format("Type mapping not found for: {}",
                                       serviceType));  // Reply 400.
            co_return;                                 // Stop.
          }

          // Guard the call.
          try {
            co_await std::visit(
                [&](auto& controller)
                    -> drogon::Task<void> {  // Dispatch to right
                                             // controller.
                  // Remove staff
                  // asynchronously
                  // with body.
                  // Chosen variant.
                  // Unexpected error.
                  co_await controller
                      ->RemoveStaffFromEntityAsync(std::move(mcb),
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
                              staffRegistry,
                              serviceType,
                              &StaffControllerBase::RemoveStaffFromEntity,
                              req,
                              std::move(callback),
                              req->body());  // Blocking mode: dispatch
                                             // RemoveStaffFromEntity with body.
    }

    // All staff routes are SECURE (rate limit + timing + token auth).
    METHOD_LIST_BEGIN  // Start the Drogon route-registration block.
        METHOD_ADD(Staff::Invite,
                   "/services/{serviceType}/staff/invite",
                   drogon::Post,
                   SECURE);  // POST invite for a {serviceType}.
    METHOD_ADD(Staff::Add,
               "/services/{serviceType}/staff/add",
               drogon::Post,
               SECURE);  // POST add for a {serviceType}.
    METHOD_ADD(Staff::Remove,
               "/services/{serviceType}/staff/remove",
               drogon::Post,
               SECURE);  // POST remove for a {serviceType}.
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
     * Concrete staff controllers, one per supported serviceType.
     */
    using StaffVariant = std::variant<
        std::shared_ptr<StaffController<Clinics>>,
        std::shared_ptr<StaffController<Pharmacies>>,  // Holds one of four
                                                       // shared staff
                                                       // controller pointers.
        std::shared_ptr<StaffController<Laboratories>>,
        std::shared_ptr<StaffController<
            RadiologyCenters>>>;  // Laboratory and radiology-center
                                  // controllers.

    /**
     * Maps a serviceType path key to its controller variant.
     */
    std::unordered_map<std::string_view, StaffVariant> staffRegistry =
        {{"clinics",
          Store::getObject<StaffController<Clinics>>()},  // Map URL keys to
                                                          // their controllers;
                                                          // "clinics" first.
         {"pharmacies", Store::getObject<StaffController<Pharmacies>>()},
         {"laboratories",
          Store::getObject<StaffController<Laboratories>>()},  // pharmacies and
                                                               // laboratories
                                                               // entries.
         {"radiologycenters",
          Store::getObject<
              StaffController<RadiologyCenters>>()}};  // radiologycenters entry
                                                       // closes the map.
  };

}  // namespace api::v2
