/**
 * @file staffcontroller.cpp
 * @brief Definitions for @ref StaffController and its explicit template
 * instantiations.
 * @details Unlike the other controller adapters, this file contains real logic:
 * request parsing, staff-management authorisation, and either a staff SQL
 * statement or an outbound invite POST to the email-sender daemon.
 *
 *          In plain words, each function here does the same three-step dance:
 * (1) read the JSON request body, (2) ask GateKeeper whether the caller is
 * allowed to manage this service's staff, and (3) only if allowed, actually do
 * the work — run SQL to add/remove a member, or POST an invitation to the email
 * daemon. If anything goes wrong, the catch block turns the exception into an
 * HTTP error reply. The macros at the end build a concrete version of the class
 * for each service entity type so the route layer can link to it.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the implementation behind the staff HTTP routes: it is where
 * permission is actually enforced before any staff change is made and where
 * invitations are sent. It is called through
 * @ref StaffControllerBase by the route layer, and it calls into @ref
 * Controller (for SQL),
 * @ref GateKeeper (for permission) and @ref Communicate (for the email POST).
 * Without it, the staff interface would have no behaviour and no one could add,
 * remove or invite staff.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - try/catch (handling errors instead of crashing):
 * https://en.cppreference.com/w/cpp/language/try_catch
 * - std::optional (a value that may be absent):
 * https://en.cppreference.com/w/cpp/utility/optional
 * - C++20 coroutines (functions that pause and resume with co_await/co_return):
 * https://en.cppreference.com/w/cpp/language/coroutines
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// The class declarations whose method bodies are defined below.
#include "controllers/staffcontroller/staffcontroller.hpp"

// Defines HTTP method constants such as drogon::HttpMethod::Post.
#include <drogon/HttpTypes.h>
// Formatting helpers (used by the project's response macros).
#include <fmt/format.h>

// Gives fixed-width whole-number types such as uint64_t (an unsigned 64-bit
// number).
#include <cstdint>
// Gives std::exception, the base type caught when something goes wrong.
#include <exception>
// Core jsoncons types used to hold and parse JSON values.
#include <jsoncons/basic_json.hpp>
// Gives jsoncons::json and its parse()/at() helpers for reading request bodies.
#include <jsoncons/json.hpp>
// Gives std::optional: a box that either holds a value or says "there is no
// value".
#include <optional>
// Gives std::string, used for the optional email-daemon response text.
#include <string>
// Gives std::string_view: a cheap, non-owning view over a stretch of text.
#include <string_view>
// Gives std::move, used to hand the callback on without copying it.
#include <utility>

// Defines Configurator and its EmailSenderConfig (where invitation emails are
// sent).
#include "configurator/configurator.hpp"
// Provides the low-level addStaff/removeStaff SQL helpers.
#include "controllers/base/controller/controller.hpp"
// The interface this class implements.
#include "controllers/staffcontroller/staffcontrollerBase.hpp"
// Drogon's coroutine support; defines drogon::Task, the pausable task type.
#include "drogon/utils/coroutine.h"
// Defines api::v2::Types::StaffData, the parsed staff fields.
#include "entities/base/types.hpp"
// Defines GateKeeper and its canManageStaff permission checks.
#include "gatekeeper/gatekeeper.hpp"
// The service locator used in the constructor to fetch shared services.
#include "store/store.hpp"
// Provides Communicate::sendRequest for the outbound invite POST.
#include "utils/communicate/communicate.hpp"
// Defines CALLBACK_, the "call me when finished" function type.
#include "utils/global/callback.hpp"
// Project-wide macros, including CRITICALMESSAGERESPONSE used in catch blocks.
#include "utils/global/global.hpp"
// Defines HTTP status constants such as OK and BAD_REQUEST.
#include "utils/global/http.hpp"

/** @brief Convenience alias for the authorisation service. */
using GateKeeper =
    api::v2::GateKeeper;  // A short nickname so we can write "GateKeeper"
                          // instead of the longer name.
/** @brief Convenience alias for the error payload produced by authorisation
 * failures. */
using HttpError =
    api::v2::Http::Error;  // Short name for the {code, message} pair returned
                           // when permission is denied.
/** @brief Convenience alias for the invite/staff fields parsed from a request
 * body. */
using StaffData =
    api::v2::Types::StaffData;  // Short name for the struct holding the staff
                                // member's details.

/**
 * @brief Resolves shared services from the global Store and snapshots the
 * email-daemon config.
 * @details `cfg_` and `gateKeeper` are service-locator handles; the
 * `EmailSenderConfig` value is copied out once so the invite path does not
 * re-read configuration per request.
 * @note Failure behaviour when a service is absent from the Store is that of
 * `Store::getObject`.
 */
template <typename T>  // One constructor definition works for any entity type
                       // T.
StaffController<T>::StaffController()
    // Fetch the shared configuration
    // service from the global Store.
    // Copy out the
    // email-daemon
    // settings once; the
    // arrow dereferences
    // the shared pointer.
    // Fetch the shared permission
    // service from the Store.
    : cfg_(Store::getObject<Configurator>()),
      email_sender_daemon_config_(cfg_->get<Configurator::EmailSenderConfig>()),
      gateKeeper(Store::getObject<GateKeeper>()) {
}

/**
 * Adds a staff member described by @p data to the service entity it names.
 * Parses a JSON body with `payload` and `id`, builds @ref StaffData and `T`,
 * checks staff-management permission against the id, and finally delegates the
 * SQL work to `Controller::addStaff`. On denial the GateKeeper's code and
 * message are returned verbatim; any thrown exception is mapped by
 * `CRITICALMESSAGERESPONSE` (500, or 503 for a busy database). Note: There is
 * no pre-parse guard: malformed JSON or a missing key is an exceptional 500
 * path here, unlike the 400 responses of @ref EntityController. callback:
 * Completion handler invoked once with (status, body). requester: Authenticated
 * caller used for the staff-management permission check. data: Raw JSON body
 * containing `id` and `payload`; must outlive the call.
 */
// Works for whichever entity type T this controller was
// instantiated with.
// Synchronous add: parse,
// authorize, then run SQL.
template <typename T>
void StaffController<T>::AddStaffToEntity(CALLBACK_&&       callback,
                                          const Requester&& requester,
                                          std::string_view  data) {
  jsoncons::json
      staff_j;  // Will hold the parsed JSON request body; jsoncons::json can
                // hold objects, arrays, strings, numbers, etc.
  HttpError error;  // Will be filled with {code, message} if the permission
                    // check fails.
  // Guard the whole operation: bad JSON or a missing key throws and is
  // handled below.
  try {
    // A parse failure or missing key throws and is handled as 500 by
    // CRITICALMESSAGERESPONSE.
    staff_j = jsoncons::json::parse(
        data);  // Turn the raw text into a JSON value we can query.
    jsoncons::json payload =
        staff_j.at("payload");     // Pull out the "payload" object holding the
                                   // new staff member's fields.
    StaffData staffData(payload);  // Convert that JSON object into a typed
                                   // StaffData struct.

    T staff(staffData);  // Build the concrete entity (e.g. a Clinic) from
                         // the staff data.

    uint64_t _id =
        staff_j.at("id").as<uint64_t>();  // Read the target service id as a
                                          // 64-bit unsigned number.

    // Permission decides before any database side effect; denial is
    // reported as-is.
    // The "!" means "if NOT allowed":
    // stop when permission is denied.
    if (!gateKeeper->canManageStaff<T>(requester, _id, error)) {
      std::move(callback)(error.code,
                          error.message);  // Report the GateKeeper's error
                                           // straight back to the caller.
      return;  // Stop here — no SQL should run when permission was
               // denied.
    }

    // Controller builds and runs the entity's add-staff SQL statement.
    // Hand off the SQL work;
    // std::move transfers the
    // callback so no copy is made.
    // Runs only if something above threw an exception.
    Controller::addStaff(staff, std::move(callback));
  } catch (const std::exception& e) {
    CRITICALMESSAGERESPONSE  // Macro: build an HTTP 500 (or 503 if the DB
                             // is busy) error reply and return it.
  }
}
/**
 * Removes a staff member from the service entity named in @p data.
 * Same request shape and authorisation contract as @ref AddStaffToEntity, but
 * delegates to `Controller::removeStaff`. Malformed input is an exceptional 500
 * path; a failed permission check returns the GateKeeper error unchanged.
 * callback: Completion handler invoked once with (status, body).
 * requester: Authenticated caller used for the staff-management permission
 * check. data: Raw JSON body containing `id` (the service) and `payload` (the
 * staff member).
 */
template <typename T>
void StaffController<T>::RemoveStaffFromEntity(
    CALLBACK_&&       callback,
    const Requester&& requester,
    std::string_view
        // Synchronous remove: parse, authorize, then run SQL.
        data) {
  // Guard against malformed JSON, just like AddStaffToEntity.
  try {
    HttpError      error;  // Filled with the error if permission is denied.
    jsoncons::json staff_j =
        jsoncons::json::parse(data);  // Parse the raw body into JSON.
    jsoncons::json payload =
        staff_j.at("payload");  // Get the staff member's fields.
    // 'id' names the service whose permissions are checked, not the staff
    // member.
    uint64_t  _id = staff_j.at("id").as<uint64_t>();  // The service id used for
                                                      // the permission check.
    StaffData staffData(
        payload);  // Convert the payload JSON into a typed struct.

    T staff(staffData);  // Build the concrete entity from the staff data.
    // If NOT allowed to manage this
    // service's staff...
    if (!gateKeeper->canManageStaff<T>(requester, _id, error)) {
      std::move(
          callback)(error.code,
                    error.message);  // Send back the permission error as-is.
      return;                        // Stop before touching the database.
    }
    // Allowed: delegate the
    // remove-staff SQL to the base.
    // Any thrown exception is turned into an error response.
    Controller::removeStaff(staff, std::move(callback));
  } catch (const std::exception& e) {
    CRITICALMESSAGERESPONSE  // Macro: reply with 500 (or 503 for a busy
                             // database).
  }
}
/**
 * Invites a staff member by handing an invite message to the email-sender
 * daemon. Parses `nominee_id` and the @ref StaffData fields from the whole
 * body, checks staff-management permission, and — if the fields parsed and the
 * invite JSON could be built — POSTs that JSON to the configured email daemon.
 * The daemon's body is relayed with 200 on success; 400 is returned when invite
 * construction or delivery fails. Note: `nominee_id` is the value passed to
 * `canManageStaff` as the permission-check target; note also that the email
 * request is a synchronous cURL call. callback: Completion handler invoked once
 * with (status, body). requester: Authenticated caller used for the
 * staff-management permission check. data: Raw JSON body containing
 * `nominee_id` and the invite fields.
 */
template <typename T>
void StaffController<T>::InviteStaffToEntity(
    CALLBACK_&&       callback,
    const Requester&& requester,
    std::string_view
        // Synchronous invite: parse, authorize, then POST an email.
        data) {
  // Guard against parse errors and network problems.
  try {
    HttpError      error;  // Filled if permission is denied.
    jsoncons::json staff_j =
        jsoncons::json::parse(data);  // Parse the whole body as JSON.
    // The nominee id doubles as the permission-check target for this
    // operation.
    uint64_t _id = staff_j.at("nominee_id")
                       .as<uint64_t>();  // Id of the person being invited; also
                                         // used for the permission check.
    StaffData staffData(staff_j);  // Build typed invite fields from the JSON.
    std::optional<std::string> response;  // Will hold the daemon's reply text;
                                          // empty means delivery failed.
    T staff(staffData);  // Build the concrete entity from the invite data.

    // If NOT allowed to invite for
    // this service...
    if (!gateKeeper->canManageStaff<T>(requester, _id, error)) {
      std::move(
          callback)(error.code,
                    error.message);  // Report the permission error unchanged.
      return;                        // Stop; do not send any email.
    }

    // parse_status records whether StaffData found all required fields;
    // toInviteJson adds the mail template and invitation link to the JSON
    // that is sent downstream.
    // Both must succeed: fields
    // parsed AND invite JSON built.
    if (staffData.parse_status && staffData.toInviteJson(staff_j)) {
      // Synchronous cURL POST; an empty optional denotes delivery
      // failure.
      response = Communicate::
          sendRequest(email_sender_daemon_config_.host,
                      email_sender_daemon_config_
                          .port,  // Send the invite to the configured email
                                  // daemon (host + port).
                      email_sender_daemon_config_.message_queue_path,
                      drogon::HttpMethod::Post,
                      staff_j.to_string());  // Use the configured queue path,
                                             // an HTTP POST, and the JSON as
                                             // the body.

      // A value means the daemon replied
      // successfully.
      if (response.has_value()) {
        // Relay the daemon's response body to the HTTP client.
        // Reply 200 with the daemon's body;
        // .value() unwraps the optional.
        // No value means the send failed.
        std::move(callback)(api::v2::Http::Status::OK, response.value());
      } else {
        // Reply 400 with a
        // diagnostic message.
        // Parsing or invite-JSON construction failed.
        std::move(callback)(api::v2::Http::Status::BAD_REQUEST,
                            "Failed to send invite.");
      }
    } else {
      // Reply 400 explaining
      // the invite could not
      // be built.
      // Any unexpected exception is turned into an error response.
      std::move(callback)(api::v2::Http::Status::BAD_REQUEST,
                          "Failed to create invite json.");
    }
  } catch (const std::exception& e) {
    CRITICALMESSAGERESPONSE  // Macro: reply with 500 (or 503 for a busy
                             // database).
  }
}

/**
 * Coroutine form of @ref AddStaffToEntity.
 * Mirrors the synchronous flow but co_awaits `canManageStaffAsync` and
 *          `Controller::addStaffAsync`. The callback is invoked from the
 * resumed coroutine; callers must co_await the returned task (the route layer
 * uses `drogon::async_run`). callback: Completion handler invoked once with
 * (status, body). requester: Authenticated caller used for the staff-management
 * permission check. data: Raw JSON body; must remain valid until the task
 * completes.
 */
// Async add: returns a pausable task.
template <typename T>
drogon::Task<void> StaffController<T>::AddStaffToEntityAsync(
    CALLBACK_&& callback, const Requester&& requester, std::string_view data) {
  jsoncons::json staff_j;  // Will hold the parsed JSON body.
  HttpError      error;    // Filled if permission is denied.
  // Guard the whole coroutine body against exceptions.
  try {
    staff_j = jsoncons::json::parse(data);  // Parse the raw body into JSON.
    jsoncons::json payload =
        staff_j.at("payload");     // Extract the new staff member's fields.
    StaffData staffData(payload);  // Convert them into a typed struct.

    T staff(staffData);  // Build the concrete entity.

    uint64_t _id =
        staff_j.at("id")
            .as<uint64_t>();  // Target service id for the permission check.

    // co_await pauses until the async permission answer
    // arrives.
    if (!co_await gateKeeper->canManageStaffAsync<T>(requester, _id, error)) {
      std::move(callback)(error.code,
                          error.message);  // Denied: report the error through
                                           // the callback.
      // Denial ends the coroutine here; no staff SQL is submitted.
      co_return;  // End this coroutine early without running SQL.
    }

    // Pause until the async
    // SQL add finishes.
    // Exceptions are turned into an error response.
    co_await Controller::addStaffAsync(staff, std::move(callback));
  } catch (const std::exception& e) {
    CRITICALMESSAGERESPONSE  // Macro: reply with 500 (or 503 for a busy
                             // database).
  }
  co_return;  // Every coroutine must explicitly finish with co_return.
}
/**
 * Coroutine form of @ref RemoveStaffFromEntity.
 * Same parsing and authorisation contract as the synchronous variant, with
 * async permission and SQL calls; denial ends the task before any database
 * work. callback: Completion handler invoked once with (status, body).
 * requester: Authenticated caller used for the staff-management permission
 * check. data: Raw JSON body; must remain valid until the task completes.
 */
// Async remove: returns a pausable task.
template <typename T>
drogon::Task<void> StaffController<T>::RemoveStaffFromEntityAsync(
    CALLBACK_&& callback, const Requester&& requester, std::string_view data) {
  // Guard against parse errors and exceptions.
  try {
    HttpError      error;  // Filled if permission is denied.
    jsoncons::json staff_j =
        jsoncons::json::parse(data);  // Parse the raw body.
    jsoncons::json payload =
        staff_j.at("payload");  // Extract the staff member's fields.
    // 'id' names the service whose permissions are checked, not the staff
    // member.
    uint64_t _id =
        staff_j.at("id").as<uint64_t>();  // The service id used for permission.
    StaffData staffData(payload);  // Convert payload into a typed struct.

    T staff(staffData);  // Build the concrete entity.
    // Pause until the permission answer arrives; "!" means
    // denied.
    if (!co_await gateKeeper->canManageStaffAsync<T>(requester, _id, error)) {
      std::move(callback)(error.code,
                          error.message);  // Report the permission error.
      co_return;                           // Stop the coroutine; skip the SQL.
    }
    // Pause until the
    // async remove-staff
    // SQL finishes.
    // Any exception is turned into an error response.
    co_await Controller::removeStaffAsync(staff, std::move(callback));
  } catch (const std::exception& e) {
    CRITICALMESSAGERESPONSE  // Macro: reply with 500 (or 503 for a busy
                             // database).
  }
  co_return;  // Mark the end of this coroutine.
}
/**
 * Coroutine form of @ref InviteStaffToEntity.
 * Mirrors the synchronous flow, with an async permission check. The
 * email-daemon POST remains a synchronous cURL call, so this coroutine may
 * block on network I/O. callback: Completion handler invoked once with (status,
 * body). requester: Authenticated caller used for the staff-management
 * permission check. data: Raw JSON body; must remain valid until the task
 * completes.
 */
// Async invite: returns a pausable task.
template <typename T>
drogon::Task<void> StaffController<T>::InviteStaffToEntityAsync(
    CALLBACK_&& callback, const Requester&& requester, std::string_view data) {
  // Guard against parse errors and network problems.
  try {
    HttpError      error;  // Filled if permission is denied.
    jsoncons::json staff_j =
        jsoncons::json::parse(data);  // Parse the whole body as JSON.
    // The nominee id doubles as the permission-check target for this
    // operation.
    uint64_t _id =
        staff_j.at("nominee_id").as<uint64_t>();  // Invitee id, also used for
                                                  // the permission check.
    StaffData staffData(staff_j);  // Build typed invite fields from the JSON.
    std::optional<std::string>
        response;          // Holds the daemon's reply; empty means failure.
    T   staff(staffData);  // Build the concrete entity.

    // Pause until the async permission answer; "!" means
    // denied.
    if (!co_await gateKeeper->canManageStaffAsync<T>(requester, _id, error)) {
      std::move(callback)(error.code,
                          error.message);  // Report the permission error.
      co_return;  // Stop the coroutine; do not send an email.
    }

    // parse_status records whether StaffData found all required fields;
    // toInviteJson adds the mail template and invitation link to the JSON
    // that is sent downstream.
    // Require both: fields parsed AND invite JSON built.
    if (staffData.parse_status && staffData.toInviteJson(staff_j)) {
      // Synchronous cURL POST; an empty optional denotes delivery
      // failure.
      response =
          Communicate::sendRequest(email_sender_daemon_config_.host,
                                   email_sender_daemon_config_
                                       .port,  // POST the invite to the
                                               // configured email daemon.
                                   email_sender_daemon_config_
                                       .message_queue_path,
                                   drogon::HttpMethod::Post,
                                   staff_j
                                       .to_string());  // Queue path, HTTP POST
                                                       // method, JSON body.

      // A value means the daemon replied.
      if (response.has_value()) {
        // Relay the daemon's response body to the HTTP client.
        // Reply 200 with the daemon's body.
        // No value means delivery failed.
        std::move(callback)(api::v2::Http::Status::OK, response.value());
      } else {
        // Reply 400 with a
        // diagnostic.
        // Could not build a valid invite.
        std::move(callback)(api::v2::Http::Status::BAD_REQUEST,
                            "Failed to send invite.");
      }
    } else {
      // Reply 400 explaining
      // the failure.
      // Any unexpected exception becomes an error response.
      std::move(callback)(api::v2::Http::Status::BAD_REQUEST,
                          "Failed to create invite json.");
    }
  } catch (const std::exception& e) {
    CRITICALMESSAGERESPONSE  // Macro: reply with 500 (or 503 for a busy
                             // database).
  }
  co_return;  // Mark the end of this coroutine.
}

// Entity definitions must be visible here for the explicit instantiations
// below, which need T's staff-related SQL statements and permission queries.
#include "entities/services/clinics/clinics.hpp"   //IWYU pragma : keep
#include "entities/services/laboratories.hpp"      // IWYU pragma: keep
#include "entities/services/pharmacies.hpp"        // IWYU pragma: keep
#include "entities/services/radiologycenters.hpp"  //IWYU pragma : keep

/**
 * @brief Forces compilation of the constructor and all member templates for one
 * service entity type.
 * @details Definitions live in this translation unit, so without these
 * instantiations the route layer's `shared_ptr<StaffController<T>>`
 * registrations would fail to link. A "translation unit" is one source file
 * plus everything it includes after preprocessing.
 * @param TYPE Concrete service entity class to instantiate.
 */
#define INSTANTIATE_STAFF_CONTROLLER(TYPE) /*NOLINT*/                          \
  template StaffController<TYPE>::StaffController();                           \
  template void                                                                \
  StaffController<TYPE>::AddStaffToEntity(CALLBACK_&&       callback,          \
                                          const Requester&& requester,         \
                                          std::string_view  data);             \
  template void                                                                \
  StaffController<TYPE>::RemoveStaffFromEntity(CALLBACK_&&       callback,     \
                                               const Requester&& requester,    \
                                               std::string_view  data);        \
  template void                                                                \
  StaffController<TYPE>::InviteStaffToEntity(CALLBACK_&&       callback,       \
                                             const Requester&& requester,      \
                                             std::string_view  data);          \
  template drogon::Task<void>                                                  \
  StaffController<TYPE>::AddStaffToEntityAsync(CALLBACK_&&       callback,     \
                                               const Requester&& requester,    \
                                               std::string_view  data);        \
  template drogon::Task<void> StaffController<                                 \
      TYPE>::RemoveStaffFromEntityAsync(CALLBACK_&&       callback,            \
                                        const Requester&& requester,           \
                                        std::string_view  data);               \
  template drogon::Task<void>                                                  \
  StaffController<TYPE>::InviteStaffToEntityAsync(CALLBACK_&&       callback,  \
                                                  const Requester&& requester, \
                                                  std::string_view  data);

// Instantiate for all entity types — each line builds one concrete staff
// controller.

INSTANTIATE_STAFF_CONTROLLER(
    Clinics)  // Build the clinic staff controller's code so the route layer can
              // link to it.
INSTANTIATE_STAFF_CONTROLLER(
    Pharmacies)  // Build the pharmacy staff controller's code so the route
                 // layer can link to it.
INSTANTIATE_STAFF_CONTROLLER(
    Laboratories)  // Build the laboratory staff controller's code so the route
                   // layer can link to it.
INSTANTIATE_STAFF_CONTROLLER(
    RadiologyCenters)  // Build the radiology-center staff controller's code so
                       // the route layer can link to it.
