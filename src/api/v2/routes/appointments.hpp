/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file appointments.hpp
 * @brief REST controller for appointment records of the appointment-providing
 * entities.
 * @details This file defines the Appointments controller, which exposes create,
 * read, update, delete and search endpoints for appointments. A {entityType}
 * part of the URL (clinics, pharmacies, laboratories or radiologycenters)
 * selects one concrete AppointmentController from a registry held in a
 * std::variant. Each handler either awaits an async controller coroutine or
 * calls executeControllerMethod for blocking database work, depending on the
 * configuration. Every handler first builds a callback that converts the
 * controller's numeric status into a standard JSON HTTP response and builds a
 * Requester from the identity attributes set by Auth.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This controller is the HTTP front door for all appointment management. Drogon
 * routes the /api/v2 appointment URLs here; the handlers then call the shared
 * dispatcher in basic/common.hpp, which calls the appointment controllers and
 * entities and ultimately the database. It depends on the Auth filter for
 * identity and on the service locator for the executor and controllers. Without
 * it there would be no way for clients to book, view, change or cancel
 * appointments through the API.
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
// Base class drogon::HttpController used by Appointments.
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
// appointment flavour.
// Provides parseIdParam, executeControllerMethod, SECURE and Helper.
#include "api/v2/basic/common.hpp"
// AppointmentController<T> template definition.
#include "controllers/appointmentcontroller/appointmentcontroller.hpp"
// Base class exposing the shared method pointers.
#include "controllers/appointmentcontroller/appointmentcontrollerbase.hpp"
// DbExecutor runs blocking database work off the IO thread.
#include "database/dbexecutor.hpp"
// drogon::async_run and drogon::Task for the async path.
#include "drogon/utils/coroutine.h"
#include "entities/appointments/clinic.hpp"  // ClinicAppointment entity type.
// LaboratoryAppointment entity type.
#include "entities/appointments/laboratory.hpp"
// PharmacyAppointment entity type.
#include "entities/appointments/pharmacy.hpp"
// RadiologyCenterAppointment entity type.
#include "entities/appointments/radiologycenter.hpp"
#include "store/store.hpp"  // Store/service locator supplies shared objects.

namespace api::v2  // The v2 API controllers live here.
{
  /**
   * @class Appointments
   * @brief REST controller for appointment records of the
   * appointment-providing entities.
   * @details A {entityType} path segment (clinics, pharmacies, laboratories,
   * radiologycenters) selects a concrete AppointmentController<T> from a
   * std::variant registry. Every handler builds a callback that maps the
   * controller's numeric status to a standard JSON HTTP response and
   *          constructs a Requester from the "clientID"/"clientGroup"
   * attributes inserted by the Auth filter, so all routes are SECURE. When
   * DatabaseConfig::async is set the *Async controller methods are awaited in
   * a drogon::Task coroutine; otherwise executeControllerMethod runs the
   * blocking permission check and query on the shared DbExecutor. Controllers
   * and the executor are shared service-locator objects.
   */
  class Appointments
      : public drogon::HttpController<
            Appointments>  // Inherit Drogon's controller template so Drogon
                           // registers these routes.
  {
   public:
    /**
     * @brief Create an appointment from the JSON request body.
     * @details POST /{entityType}/create (SECURE). The async path looks up
     * the controller and co_awaits CreateAsync; an unknown entityType
     * yields 400. The sync path delegates to executeControllerMethod with
     * the synchronous Create method.
     * @param[in] req Request carrying the body and Auth identity
     * attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] entityType Registry key taken from the path.
     */
    void Create(const drogon::HttpRequestPtr&                 req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string&
            entityType)  // HTTP handler: req is the request, callback
                         // answers it, entityType comes from the URL.
    {
      const bool async = asyncDb_;  // Copy the cached configuration flag:
                                    // which database mode to use.
      if (async)  // Async mode: run the controller coroutine instead of
                  // blocking a worker.
      {
        auto cb = std::move(callback);  // Move the callback into the coroutine
                                        // so it outlives this function.
        drogon::async_run(
            [this, req, cb, entityType]() mutable
                -> drogon::Task<void>  // Start a coroutine capturing
                                       // everything it needs by value.
            {
              CALLBACK_ mcb = [cb](int code,
                                  const std::string&
                                      content)  // Adapter: turn the
                                                // controller's (code, JSON)
                                                // reply into an HTTP response.
              {
                using Reply = std::function<void(
                    const drogon::HttpResponsePtr&)>;  // Short alias for the
                                                       // response callback
                                                       // type.
                switch (code)  // Translate the numeric status into
                               // the matching JSON reply.
                {
                  // Create succeeded.
                  case api::v2::Http::Status::OK:
                    Helper::successResponse(content,
                        Reply(cb));  // Send 200 with the
                                     // returned JSON body.
                    break;           // Leave the switch.
                  case api::v2::Http::Status::
                      INTERNAL_SERVER_ERROR:  // Internal failure.
                    Helper::failureResponse(content,
                        Reply(cb));  // Send a tagged 500 body.
                    break;           // Leave the switch.
                  default:           // Any specific error code.
                    Helper::errorResponse(
                        static_cast<drogon::HttpStatusCode>(code), content,
                        Reply(cb));  // Reuse the number as the
                                     // HTTP status.
                    break;           // Leave the switch.
                }
              };

              Requester requester(
                  req->getAttributes()->get<uint64_t>("clientID"),
                  req->getAttributes()->get<std::string>(
                      "clientGroup"));  // Build the caller identity
                                        // from Auth attributes for
                                        // permission checks.

              auto ctlIt = appointmentRegistry.find(
                  entityType);  // Find the controller variant for
                                // this URL type.
              if (ctlIt == appointmentRegistry.end())  // Unknown entity type
                                                       // cannot be handled.
              {
                std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                    fmt::format("Type mapping not found for: {}",
                        entityType));  // Reply 400 with the
                                       // offending type.
                co_return;             // End the coroutine; the response is
                                       // already sent.
              }

              try  // The controller call may throw.
              {
                co_await std::visit(
                    [&](auto& controller) -> drogon::Task<void>
                    {  // Pick the stored controller type and wait
                       // for its coroutine.
                      co_await controller->CreateAsync(std::move(mcb),
                          std::move(requester),
                          req->body());  // Create the appointment
                                         // asynchronously using
                                         // the JSON body.
                    },
                    ctlIt->second);  // Pass the chosen controller
                                     // variant to std::visit.
              }
              catch (const std::exception& e)  // Any unexpected error.
              {
                Helper::failureResponse(e.what(),
                    std::move(cb));  // Reply 500 with the error text.
              }
            });

        return;  // The coroutine owns the rest; leave the handler now.
      }

      executeControllerMethod(*dbExecutor, appointmentRegistry, entityType,
          &AppointmentControllerBase::Create, req, std::move(callback),
          req->body());  // Blocking mode: shared dispatcher runs Create
                         // on the DB executor with the body.
    }

    /**
     * @brief Read appointments using the criteria in the JSON request body.
     * @details POST /{entityType}/read (SECURE). Async path co_awaits
     * ReadAsync after the registry lookup; sync path uses
     * executeControllerMethod. Unknown entityType yields 400.
     * @param[in] req Request carrying the body and Auth identity
     * attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] entityType Registry key taken from the path.
     */
    void Read(const drogon::HttpRequestPtr&                   req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string&
            entityType)  // HTTP handler for reading appointments.
    {
      const bool async = asyncDb_;  // Which database mode is configured.
      if (async)                    // Async coroutine path.
      {
        auto cb = std::move(callback);  // Move callback into the coroutine.
        drogon::async_run(
            [this, req, cb, entityType]() mutable
                -> drogon::Task<void>  // Launch the coroutine with
                                       // captured state.
            {
              CALLBACK_ mcb = [cb](int code,
                                  const std::string&
                                      content)  // Adapt numeric controller
                                                // status to an HTTP response.
              {
                using Reply = std::function<void(
                    const drogon::HttpResponsePtr&)>;  // Alias for the
                                                       // response callback
                                                       // type.
                switch (code)  // Map the status code to a reply.
                {
                  // Success.
                  case api::v2::Http::Status::OK:
                    Helper::successResponse(content,
                        Reply(cb));  // Send 200 with body as-is.
                    break;           // Leave the switch.
                  case api::v2::Http::Status::
                      INTERNAL_SERVER_ERROR:  // Failure.
                    Helper::failureResponse(content,
                        Reply(cb));  // Send tagged 500.
                    break;           // Leave the switch.
                  default:           // Other error.
                    Helper::errorResponse(
                        static_cast<drogon::HttpStatusCode>(code), content,
                        Reply(cb));  // Reuse the code as HTTP
                                     // status.
                    break;           // Leave the switch.
                }
              };

              Requester requester(
                  req->getAttributes()->get<uint64_t>("clientID"),
                  req->getAttributes()->get<std::string>(
                      "clientGroup"));  // Build caller identity for
                                        // permissions.

              auto ctlIt = appointmentRegistry.find(
                  entityType);  // Look up the controller for this
                                // type.
              if (ctlIt == appointmentRegistry.end())  // Unknown type.
              {
                std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                    fmt::format("Type mapping not found for: {}",
                        entityType));  // Reply 400.
                co_return;             // Stop the coroutine.
              }

              try  // Guard the controller call.
              {
                co_await std::visit(
                    [&](auto& controller) -> drogon::Task<void>
                    {  // Dispatch to the correct controller type.
                      co_await controller->ReadAsync(std::move(mcb),
                          std::move(requester),
                          req->body());  // Read asynchronously
                                         // using criteria in the
                                         // body.
                    },
                    ctlIt->second);  // The selected variant.
              }
              catch (const std::exception& e)  // Unexpected error.
              {
                Helper::failureResponse(e.what(), std::move(cb));  // Reply 500.
              }
            });

        return;  // Coroutine handles the rest.
      }

      executeControllerMethod(*dbExecutor, appointmentRegistry, entityType,
          &AppointmentControllerBase::Read, req, std::move(callback),
          req->body());  // Blocking mode: dispatch Read with the body.
    }

    /**
     * @brief Update the appointment identified by the "id" query parameter.
     * @details PUT /{entityType}/update (SECURE). A missing/invalid id is
     * answered 400 by parseIdParam and returns before any work is
     * scheduled. The async path co_awaits UpdateAsync with the parsed id;
     * the sync path uses executeControllerMethod.
     * @param[in] req Request carrying the body, id parameter and Auth
     * identity attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] entityType Registry key taken from the path.
     * @pre The "id" query parameter must be a valid integer (enforced at
     * runtime).
     */
    void Update(const drogon::HttpRequestPtr&                 req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string&
            entityType)  // HTTP handler for updating an appointment.
    {
      // Reject a bad id with 400 before scheduling the async coroutine.
      auto id = parseIdParam(req, "id",
          callback);  // Parse the id safely; on failure the callback
                      // already got a 400.
      if (!id)        // No id means the error response was sent.
        return;       // Leave early so no second response is produced.

      const bool async = asyncDb_;  // Which database mode is configured.
      if (async)                    // Async coroutine path.
      {
        auto cb = std::move(callback);  // Move callback into the coroutine.
        drogon::async_run(
            [this, req, cb, entityType, id]() mutable
                -> drogon::Task<void>  // Capture the parsed id as well.
            {
              CALLBACK_ mcb = [cb](int               code,
                                  // Adapt controller status to HTTP.
                                  const std::string& content)
              {
                using Reply = std::function<void(
                    // Response callback alias.
                    const drogon::HttpResponsePtr&)>;
                switch (code)  // Map status to reply.
                {
                  // Success.
                  case api::v2::Http::Status::OK:
                    Helper::successResponse(content,
                        Reply(cb));  // Send 200 with body.
                    break;           // Leave the switch.
                  case api::v2::Http::Status::
                      INTERNAL_SERVER_ERROR:  // Failure.
                    Helper::failureResponse(content,
                        Reply(cb));  // Send tagged 500.
                    break;           // Leave the switch.
                  default:           // Other error.
                    Helper::errorResponse(
                        static_cast<drogon::HttpStatusCode>(code), content,
                        Reply(cb));  // Reuse numeric status.
                    break;           // Leave the switch.
                }
              };

              Requester requester(
                  req->getAttributes()->get<uint64_t>("clientID"),
                  req->getAttributes()->get<std::string>(
                      "clientGroup"));  // Caller identity for
                                        // permissions.

              auto ctlIt = appointmentRegistry.find(
                  entityType);  // Look up the controller for this
                                // type.
              if (ctlIt == appointmentRegistry.end())  // Unknown type.
              {
                std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                    fmt::format("Type mapping not found for: {}",
                        entityType));  // Reply 400.
                co_return;             // Stop.
              }

              try  // Guard the call.
              {
                co_await std::visit(
                    [&](auto& controller) -> drogon::Task<void>
                    {  // Dispatch to the right controller.
                      co_await controller->UpdateAsync(std::move(mcb),
                          std::move(requester), req->body(),
                          id);  // Update asynchronously with body
                                // and parsed id.
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

      executeControllerMethod(*dbExecutor, appointmentRegistry, entityType,
          &AppointmentControllerBase::Update, req, std::move(callback),
          req->body(),
          *id);  // Blocking mode: dispatch Update with body and
                 // dereferenced id.
    }
    /**
     * @brief Delete the appointment identified by the "id" query parameter.
     * @details DELETE {entityType}/delete (SECURE). A missing/invalid id is
     * answered 400 and returns early. The async path co_awaits DeleteAsync;
     * the sync path uses executeControllerMethod.
     * @param[in] req Request carrying the id parameter and Auth identity
     * attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] entityType Registry key taken from the path.
     */
    void Delete(const drogon::HttpRequestPtr&                 req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string&
            entityType)  // HTTP handler for deleting an appointment.
    {
      // Reject a bad id with 400 before scheduling the async coroutine.
      auto id = parseIdParam(req, "id",
          callback);  // Parse id safely; 400 already sent on failure.
      if (!id)        // No id means an error response was sent.
        return;       // Leave early.

      const bool async = asyncDb_;  // Which database mode is configured.
      if (async)                    // Async coroutine path.
      {
        auto cb = std::move(callback);  // Move callback into the coroutine.
        drogon::async_run(
            [this, req, cb, entityType,
                id]() mutable -> drogon::Task<void>  // Capture the parsed id.
            {
              CALLBACK_ mcb = [cb](int               code,
                                  // Adapt controller status to HTTP.
                                  const std::string& content)
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
                        content, Reply(cb));
                    break;  // Leave the switch.
                  case api::v2::Http::Status::
                      INTERNAL_SERVER_ERROR:  // Failure.
                    Helper::failureResponse(content,
                        Reply(cb));  // Send tagged 500.
                    break;           // Leave the switch.
                  default:           // Other error.
                    Helper::errorResponse(
                        static_cast<drogon::HttpStatusCode>(code), content,
                        Reply(cb));  // Reuse numeric status.
                    break;           // Leave the switch.
                }
              };

              Requester requester(
                  req->getAttributes()->get<uint64_t>("clientID"),
                  req->getAttributes()->get<std::string>(
                      "clientGroup"));  // Caller identity for
                                        // permissions.

              auto ctlIt = appointmentRegistry.find(
                  entityType);  // Look up the controller for this
                                // type.
              if (ctlIt == appointmentRegistry.end())  // Unknown type.
              {
                std::move(mcb)(api::v2::Http::Status::BAD_REQUEST,
                    fmt::format("Type mapping not found for: {}",
                        entityType));  // Reply 400.
                co_return;             // Stop.
              }

              try  // Guard the call.
              {
                co_await std::visit(
                    [&](auto& controller) -> drogon::Task<void>
                    {  // Dispatch to the right controller.
                      co_await controller->DeleteAsync(std::move(mcb),
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

      executeControllerMethod(*dbExecutor, appointmentRegistry, entityType,
          &AppointmentControllerBase::Delete, req, std::move(callback),
          *id);  // Blocking mode: dispatch Delete by id.
    }
    /**
     * @brief Search appointments using the criteria in the JSON request
     * body.
     * @details No METHOD_ADD route currently binds this handler; when
     * invoked it is dispatched only through executeControllerMethod (no
     * async coroutine path in this controller).
     * @param[in] req Request carrying the body and Auth identity
     * attributes.
     * @param[in,out] callback Response callback, consumed exactly once.
     * @param[in] entityType Registry key taken from the path.
     */
    void Search(const drogon::HttpRequestPtr&                 req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& entityType)  // HTTP-style handler, currently not
                                        // registered as a route.
    {
      executeControllerMethod(*dbExecutor, appointmentRegistry, entityType,
          &AppointmentControllerBase::Search, req, std::move(callback),
          req->body());  // Blocking dispatch of Search with the JSON
                         // body.
    }

    // Register the CRUD routes; SECURE adds rate limiting, timing and token
    // auth.
    METHOD_LIST_BEGIN  // Start the Drogon route-registration block.
        METHOD_ADD(Appointments::Create, "/{entityType}/create", drogon::Post,
            SECURE);  // POST create for a {entityType}.
    METHOD_ADD(Appointments::Read, "/{entityType}/read", drogon::Post,
        SECURE);  // POST read for a {entityType}.
    METHOD_ADD(Appointments::Update, "/{entityType}/update", drogon::Put,
        SECURE);  // PUT update for a {entityType}.
    METHOD_ADD(Appointments::Delete, "{entityType}/delete", drogon::Delete,
        SECURE);    // DELETE for a {entityType}.
   METHOD_LIST_END  // End the route-registration block.

       private :
       /** Shared pool that runs blocking database work (comment retained:
          shared with every route). */
       std::shared_ptr<DbExecutor>
               dbExecutor =
                   Store::getObject<DbExecutor>();  // runs the blocking
                                                    // database work, shared
                                                    // with every route
    /**
     * Whether the configured DB mode uses async controller coroutines
     * instead of DbExecutor.
     */
    const bool asyncDb_      = Store::cachedObject<Configurator>()
                                   ->get<Configurator::DatabaseConfig>()
                                   .async;  // Cached once: selects async
                                            // coroutines or blocking executor.
    /**
     * Concrete appointment controllers, one per supported entity type.
     */
    using AppointmentVariant =  // A variant can hold exactly one of these
                                // four shared controller pointers.
        std::variant<std::shared_ptr<AppointmentController<ClinicAppointment>>,
            std::shared_ptr<AppointmentController<PharmacyAppointment>>,
            std::shared_ptr<AppointmentController<LaboratoryAppointment>>,
            std::shared_ptr<AppointmentController<
                RadiologyCenterAppointment>>>;  // Clinic, pharmacy,
                                                // laboratory and
                                                // radiology-center
                                                // controllers.

    /**
     * Maps an entityType path key to its controller variant.
     */
    std::unordered_map<std::string_view, AppointmentVariant>
        appointmentRegistry = {// Map from URL key (e.g. "clinics") to the
                               // controller that handles it.
            {"clinics", Store::getObject<AppointmentController<
                            ClinicAppointment>>()},  // "clinics" -> clinic
                                                     // appointment controller.
            {"pharmacies",
                Store::getObject<AppointmentController<
                    PharmacyAppointment>>()},  // "pharmacies" -> pharmacy
                                               // appointment controller.
            {"laboratories",
                Store::getObject<AppointmentController<
                    LaboratoryAppointment>>()},  // "laboratories" ->
                                                 // laboratory appointment
                                                 // controller.
            {"radiologycenters",
                Store::getObject<AppointmentController<
                    RadiologyCenterAppointment>>()}};  // "radiologycenters"
                                                       // ->
                                                       // radiology-center
                                                       // controller.
  };

}  // namespace api::v2
