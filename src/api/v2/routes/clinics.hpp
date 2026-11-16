/**
 * @file clinics.hpp
 * @brief REST controller for clinic-related services (patients, visits and
 * visit documents).
 * @details This file defines the Clinic controller, which exposes create, read,
 * update, delete, search and get-visits endpoints for clinic data. A
 * {serviceType} part of the URL selects one concrete ClinicController from a
 * std::variant registry covering patients, visits, visit documents and related
 * records. Search and GetVisits are special: they are hard-wired to the
 *          "patients" controller. Depending on configuration each handler
 * either awaits an async coroutine or uses executeControllerMethod for blocking
 * database work, and each converts the controller's numeric status into a
 * standard JSON HTTP response.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This controller is the HTTP front door for everything that happens inside a
 * clinic: patient records, visits, prescriptions, requests and reports. Drogon
 * routes the /api/v2 clinic URLs here; handlers use the shared dispatcher to
 * reach clinic controllers, entities and the database. It relies on the Auth
 * filter for identity and the service locator for shared objects. Without it
 * there would be no API to manage patients or their visits.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - REST create/read/update/delete (CRUD) routes:
 * https://developer.mozilla.org/en-US/docs/Web/HTTP/Methods
 * - std::variant (a type-safe union):
 * https://en.cppreference.com/w/cpp/utility/variant
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
// Base class drogon::HttpController used by Clinic.
#include <drogon/HttpController.h>
// Request type carrying body, attributes and URL parameters.
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>  // Response type built by the JSON helpers.
#include <drogon/HttpTypes.h>     // HTTP verb and status constants.
#include <drogon/drogon.h>        // Core Drogon definitions.

#include <functional>  // std::function is the type of the response callback.
#include <memory>  // std::shared_ptr for the shared executor and controllers.
#include <string>  // std::string for URLs, keys and messages.
#include <string_view>    // std::string_view keys the registry map cheaply.
#include <unordered_map>  // The registry maps type names to controllers.
#include <utility>        // std::move for consuming callbacks.
// std::variant holds one of several controller pointer types.
#include <variant>

// Shared dispatch helpers, concrete controllers and entity types for each
// clinic service.
// Provides parseIdParam, executeControllerMethod, SECURE and Helper.
#include "api/v2/basic/common.hpp"
// ClinicController<T> template definition.
#include "controllers/cliniccontroller/cliniccontroller.hpp"
// Base class exposing the shared method pointers.
#include "controllers/cliniccontroller/cliniccontrollerbase.hpp"
// DbExecutor runs blocking database work off the IO thread.
#include "database/dbexecutor.hpp"
// drogon::async_run and drogon::Task for the async path.
#include "drogon/utils/coroutine.h"
#include "entities/services/clinics/patient/health.hpp"  // Health entity type.
// Patient entity type.
#include "entities/services/clinics/patient/patient.hpp"
// PatientDrugs entity type.
#include "entities/services/clinics/patient/patientdrugs.hpp"
// Reports entity type.
#include "entities/services/clinics/patient/reports.hpp"
// PaidServices entity type.
#include "entities/services/clinics/visits/paidservices.hpp"
// Prescriptions entity type.
#include "entities/services/clinics/visits/prescriptions.hpp"
// Requests entity type.
#include "entities/services/clinics/visits/requests.hpp"
// VisitDrugs entity type.
#include "entities/services/clinics/visits/visitdrugs.hpp"
#include "entities/services/clinics/visits/visits.hpp"  // Visits entity type.
#include "store/store.hpp"  // Store/service locator supplies shared objects.

// The v2 API controllers live here.
namespace api::v2 {
  /**
   * @class Clinic
   * @brief REST controller for clinic-related services (patients, visits and
   * visit documents).
   * @details A {serviceType} path segment selects a ClinicController<T> from
   * a variant registry (patients, visits, visitDrugs, requests,
   * prescriptions, paidservices, reports, patientdrugs, health). Search and
   * GetVisits are hard-coded to the "patients" controller. Handlers use the
   * shared callback pattern to map controller status codes to JSON HTTP
   *          responses, build a Requester from Auth attributes, and run async
   * coroutines or the DbExecutor-backed dispatch according to
   * DatabaseConfig::async. All routes are SECURE.
   */
  class Clinic
      // Inherit Drogon's controller
      // template so Drogon registers
      // these routes.
      : public drogon::HttpController<Clinic> {
   public:
    /**
     * @brief Create a clinic-service entity from the JSON request body.
     * @details POST /{serviceType}/create (SECURE). Async path co_awaits
     * CreateAsync; sync path uses executeControllerMethod. An unknown
     * serviceType yields 400.
     * @param[in] req Request carrying the body and Auth identity
     * attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] serviceType Registry key taken from the path.
     */
    // HTTP handler creating a
    // clinic-service entity.
    void Create(const drogon::HttpRequestPtr&                         req,
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

          auto ctlIt = clinicRegistry.find(
              serviceType);  // Find controller for this URL type.
          // Unknown type.
          if (ctlIt == clinicRegistry.end()) {
            std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                           fmt::format("Type mapping not found for: {}",
                                       serviceType));  // Reply 400.
            co_return;                                 // End coroutine.
          }

          // Guard the controller call.
          try {
            co_await std::visit(
                [&](auto& controller)
                    -> drogon::Task<void> {  // Dispatch to the correct
                                             // controller type.
                  // Create asynchronously
                  // from the body.
                  // The selected variant.
                  // Unexpected error.
                  co_await controller->CreateAsync(std::move(mcb),
                                                   std::move(requester),
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
                              clinicRegistry,
                              serviceType,
                              &ClinicControllerBase::Create,
                              req,
                              std::move(callback),
                              req->body());  // Blocking mode: dispatch Create
                                             // with body.
    }

    /**
     * @brief Read clinic-service entities using the criteria in the JSON
     * request body.
     * @details POST /{serviceType}/read (SECURE). Async path co_awaits
     * ReadAsync; sync path uses executeControllerMethod. An unknown
     * serviceType yields 400.
     * @param[in] req Request carrying the body and Auth identity
     * attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] serviceType Registry key taken from the path.
     */
    // HTTP handler for reading clinic services.
    void Read(const drogon::HttpRequestPtr&                         req,
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
              clinicRegistry.find(serviceType);  // Look up controller for type.
          // Unknown type.
          if (ctlIt == clinicRegistry.end()) {
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
                  // Read asynchronously
                  // with criteria in body.
                  // Chosen variant.
                  // Unexpected error.
                  co_await controller->ReadAsync(std::move(mcb),
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
                              clinicRegistry,
                              serviceType,
                              &ClinicControllerBase::Read,
                              req,
                              std::move(callback),
                              req->body());  // Blocking mode: dispatch Read
                                             // with body.
    }

    /**
     * @brief Update the clinic-service entity identified by the "id" query
     * parameter.
     * @details PUT /{serviceType}/update (SECURE). A bad id yields 400 and
     * returns early; async path co_awaits UpdateAsync with the parsed id,
     * sync path uses executeControllerMethod.
     * @param[in] req Request carrying the body, id parameter and Auth
     * identity attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] serviceType Registry key taken from the path.
     */
    // HTTP handler for updating a clinic service.
    void Update(const drogon::HttpRequestPtr&                         req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                const std::string& serviceType) {
      // Reject a bad id with 400 before scheduling the async coroutine.
      auto id =
          parseIdParam(req, "id", callback);  // Parse id; 400 sent on failure.
      if (!id)   // No id means error response was sent.
        return;  // Leave early.

      const bool async = asyncDb_;  // Which database mode is configured.
      // Async coroutine path.
      if (async) {
        auto cb = std::move(callback);  // Move callback into coroutine.
        // Capture parsed id too.
        drogon::async_run([this, req, cb, serviceType, id]() mutable
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
              clinicRegistry.find(serviceType);  // Look up controller for type.
          // Unknown type.
          if (ctlIt == clinicRegistry.end()) {
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
                  // Update asynchronously with body
                  // and id.
                  // Chosen variant.
                  // Unexpected error.
                  co_await controller->UpdateAsync(std::move(mcb),
                                                   std::move(requester),
                                                   req->body(),
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
                              clinicRegistry,
                              serviceType,
                              &ClinicControllerBase::Update,
                              req,
                              std::move(callback),
                              req->body(),
                              *id);  // Blocking mode: dispatch Update with body
                                     // and id.
    }
    /**
     * @brief Delete the clinic-service entity identified by the "id" query
     * parameter.
     * @details DELETE /{serviceType}/delete (SECURE). A bad id yields 400
     * and returns early; async path co_awaits DeleteAsync, sync path uses
     * executeControllerMethod.
     * @param[in] req Request carrying the id parameter and Auth identity
     * attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] serviceType Registry key taken from the path.
     */
    // HTTP handler for deleting a clinic service.
    void Delete(const drogon::HttpRequestPtr&                         req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                const std::string& serviceType) {
      // Reject a bad id with 400 before scheduling the async coroutine.
      auto id =
          parseIdParam(req, "id", callback);  // Parse id; 400 sent on failure.
      if (!id)   // No id means error response was sent.
        return;  // Leave early.

      const bool async = asyncDb_;  // Which database mode is configured.
      // Async coroutine path.
      if (async) {
        auto cb = std::move(callback);  // Move callback into coroutine.
        // Capture parsed id.
        drogon::async_run([this, req, cb, serviceType, id]() mutable
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
              clinicRegistry.find(serviceType);  // Look up controller for type.
          // Unknown type.
          if (ctlIt == clinicRegistry.end()) {
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
                              clinicRegistry,
                              serviceType,
                              &ClinicControllerBase::Delete,
                              req,
                              std::move(callback),
                              *id);  // Blocking mode: dispatch Delete by id.
    }
    /**
     * @brief Search patients using the criteria in the JSON request body.
     * @details POST /patients/search (SECURE). Always dispatched against
     * the "patients" controller; async path co_awaits SearchAsync, sync
     * path uses executeControllerMethod.
     * @param[in] req Request carrying the body and Auth identity
     * attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     */
    // HTTP handler; no serviceType parameter because
    // it always searches patients.
    void Search(
        const drogon::HttpRequestPtr&                         req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
      const bool async = asyncDb_;  // Which database mode is configured.
      // Async coroutine path.
      if (async) {
        auto cb = std::move(callback);  // Move callback into coroutine.
        // Launch coroutine with
        // captured state.
        drogon::async_run([this, req, cb]() mutable -> drogon::Task<void> {
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
              clinicRegistry.find("patients");  // Search is fixed to the
                                                // "patients" controller.
          // Should not happen, but guard anyway.
          if (ctlIt == clinicRegistry.end()) {
            std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                           // Reply 400 with fixed message.
                           "Type mapping not found for: patients");
            co_return;  // Stop.
          }

          // Guard the call.
          try {
            co_await std::visit(
                [&](auto& controller)
                    -> drogon::Task<void> {  // Dispatch to the patients
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
                              clinicRegistry,
                              "patients",
                              &ClinicControllerBase::Search,
                              req,
                              std::move(callback),
                              req->body());  // Blocking mode: Search fixed to
                                             // "patients" with body.
    }

    /**
     * @brief List the visits of the patient identified by the "patient_id"
     * query parameter.
     * @details GET /patients/getvisits (SECURE). A bad patient_id yields
     * 400 and returns early; the "patients" controller handles the request
     * via GetVisitsAsync or GetVisits.
     * @param[in] req Request carrying the patient_id parameter and Auth
     * identity attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     */
    // HTTP handler listing a patient's visits.
    void GetVisits(
        const drogon::HttpRequestPtr&                         req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
      // Reject a bad patient_id with 400 before scheduling the async
      // coroutine.
      auto patient_id = parseIdParam(req,
                                     "patient_id",
                                     callback);  // Parse the patient id safely;
                                                 // 400 on failure.
      if (!patient_id)              // No id means response was sent.
        return;                     // Leave early.
      const bool async = asyncDb_;  // Which database mode is configured.
      // Async coroutine path.
      if (async) {
        auto cb = std::move(callback);  // Move callback into coroutine.
        const uint64_t pid =
            *patient_id;  // Copy the id out of the optional into a
                          // plain 64-bit integer for capture.
        // Launch coroutine
        // capturing the plain id.
        drogon::async_run([this, req, cb, pid]() mutable -> drogon::Task<void> {
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
              clinicRegistry.find("patients");  // GetVisits is fixed to the
                                                // "patients" controller.
          // Should not happen; guard anyway.
          if (ctlIt == clinicRegistry.end()) {
            std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                           // Reply 400.
                           "Type mapping not found for: patients");
            co_return;  // Stop.
          }

          // Guard the call.
          try {
            co_await std::visit(
                [&](auto& controller)
                    -> drogon::Task<void> {  // Dispatch to the patients
                                             // controller.
                  // List visits asynchronously
                  // by patient id.
                  // Chosen variant.
                  // Unexpected error.
                  co_await controller->GetVisitsAsync(std::move(mcb),
                                                      std::move(requester),
                                                      pid);
                },
                ctlIt->second);
          } catch (const std::exception& e) {
            Helper::failureResponse(e.what(), std::move(cb));  // Reply 500.
          }
        });

        return;  // Coroutine handles rest.
      }

      executeControllerMethod(*dbExecutor,
                              clinicRegistry,
                              "patients",
                              &ClinicControllerBase::GetVisits,
                              req,
                              std::move(callback),
                              *patient_id);  // Blocking mode: GetVisits on
                                             // "patients" with patient id.
    }

    // All routes are SECURE; Search and GetVisits are bound to the
    // "patients" controller.
    METHOD_LIST_BEGIN  // Start the Drogon route-registration block.
        METHOD_ADD(Clinic::Create,
                   "/{serviceType}/create",
                   drogon::Post,
                   SECURE);  // POST create for a {serviceType}.
    METHOD_ADD(Clinic::Read,
               "/{serviceType}/read",
               drogon::Post,
               SECURE);  // POST read for a {serviceType}.
    METHOD_ADD(Clinic::Update,
               "/{serviceType}/update",
               drogon::Put,
               SECURE);  // PUT update for a {serviceType}.
    METHOD_ADD(Clinic::Delete,
               "/{serviceType}/delete",
               drogon::Delete,
               SECURE);  // DELETE for a {serviceType}.
    METHOD_ADD(Clinic::Search,
               "/patients/search",
               drogon::HttpMethod::Post,
               SECURE);  // POST search fixed to patients.
    METHOD_ADD(Clinic::GetVisits,
               "/patients/getvisits",
               drogon::Get,
               SECURE);  // GET a patient's visits.
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
     * Concrete clinic-service controllers, one per supported serviceType.
     */
    using ClinicVariant =  // Holds one of nine shared clinic controller
                           // pointers.
        std::variant<
            std::shared_ptr<ClinicController<Patient>>,
            std::shared_ptr<ClinicController<Visits>>,
            // patient, visits and visitDrugs controllers.
            std::shared_ptr<ClinicController<VisitDrugs>>,
            std::shared_ptr<ClinicController<Requests>>,
            std::shared_ptr<ClinicController<Prescriptions>>,
            std::shared_ptr<
                ClinicController<PaidServices>>,  // requests, prescriptions and
                                                  // paidservices controllers.
            std::shared_ptr<ClinicController<Reports>>,
            std::shared_ptr<ClinicController<PatientDrugs>>,
            std::shared_ptr<ClinicController<Health>>>;  // reports,
                                                         // patientdrugs and
                                                         // health controllers.

    /**
     * Maps a serviceType path key to its controller variant.
     */
    std::unordered_map<std::string_view, ClinicVariant> clinicRegistry =
        {{"patients",
          Store::getObject<ClinicController<Patient>>()},  // Map URL keys to
                                                           // their controllers;
                                                           // "patients" first.
         {"visits", Store::getObject<ClinicController<Visits>>()},
         {"visitDrugs",
          Store::getObject<ClinicController<VisitDrugs>>()},  // visits and
                                                              // visitDrugs
                                                              // entries.
         {"requests", Store::getObject<ClinicController<Requests>>()},
         {"prescriptions",
          Store::getObject<
              ClinicController<Prescriptions>>()},  // requests and
                                                    // prescriptions
                                                    // entries.
         {"paidservices", Store::getObject<ClinicController<PaidServices>>()},
         {"reports",
          Store::getObject<ClinicController<Reports>>()},  // paidservices and
                                                           // reports entries.
         {"patientdrugs", Store::getObject<ClinicController<PatientDrugs>>()},
         {"health",
          Store::getObject<ClinicController<Health>>()}};  // patientdrugs and
                                                           // health entries
                                                           // close the map.
  };
}  // namespace api::v2
