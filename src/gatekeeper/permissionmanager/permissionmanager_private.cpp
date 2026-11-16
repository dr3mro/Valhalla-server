/**
 * @file permissionmanager_private.cpp
 * @brief The helper functions that turn permission JSON into allow/deny
 * answers. (Originally: "Implementation for permissionmanager_private: Requires
 * staff membership plus the @p powerlevel capability bit.")
 * @details These functions look up the requester in a service's
 * owner/admin/staff fields, decode the numeric permission mask, validate
 * creation payloads, and combine the basic checks into rules like "owner,
 * admin, or staff with CAN_READ". Every denial is explained through an
 * Http::Error.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It is the engine behind PermissionManager. Keeping the detailed JSON parsing
 * and identity comparisons here means every entity family gets identical rules.
 * Without it, the public manager would have no way to interpret the database's
 * permission document.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - JSON objects and arrays: https://www.json.org/json-en.html
 * - Structured bindings (auto [a, b]):
 * https://en.cppreference.com/w/cpp/language/structured_binding
 * - Exceptions and try/catch:
 * https://en.cppreference.com/w/cpp/language/try_catch
 * - HTTP 403 Forbidden:
 * https://developer.mozilla.org/en-US/docs/Web/HTTP/Status/403
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// The declarations implemented here.
#include "gatekeeper/permissionmanager/permissionmanager_private.hpp"

#include <fmt/core.h>  // Formatting helpers used for error messages.
// fmt::format, which builds strings from a template and arguments.
#include <fmt/format.h>

#include <cstddef>    // size_t, used for positions in a string.
#include <cstdint>    // uint64_t for ids and masks.
#include <exception>  // std::exception, caught around JSON conversions.
// jsoncons::json, the permission document type.
#include <jsoncons/basic_json.hpp>
// Additional jsoncons helpers for iterating objects.
#include <jsoncons/json.hpp>
#include <optional>  // std::optional for staff entries that may not exist.
#include <string>    // std::string for names, keys and messages.

// PowerLevel and StaffPermission bit masks.
#include "gatekeeper/permissionmanager/permissions.hpp"
#include "utils/global/http.hpp"  // Http::Error and status codes for denials.
// Requester, the caller whose identity is checked.
#include "utils/global/requester.hpp"

using PermissionManagerPrivate =
    api::v2::PermissionManagerPrivate;  // Short alias so definitions below can
                                        // omit the namespace.
using PowerLevel = Permissions::PowerLevel;  // Short alias for the bit-mask
                                             // type used in comparisons.

/**
 * Requires staff membership plus the @p powerlevel capability bit.
 * error: FORBIDDEN with a reason when the requester is not staff.
 */
bool PermissionManagerPrivate::hasPermission(
    const Requester& requester,
    const std::optional<jsoncons::json>&
        permissions_j,  // Definition of the staff-capability check (signature
                        // continues next line).
    const Permissions::PowerLevel& powerlevel,
    const std::string&             service_name,
    Http::Error& error)  // Rest of hasPermission's signature: the required bit,
                         // a name for messages, and the error output.
{
  auto entityStaffPermissions =
      isStaffOfService(requester,
                       permissions_j,
                       service_name,
                       error);  // First find the caller's staff entry; it also
                                // fills in error on failure.

  if (!entityStaffPermissions.has_value())  // No staff entry means the caller
                                            // is not on this service's team.
  {
    error.code = Http::Status::FORBIDDEN;  // 403 Forbidden: malformed data
                                           // is treated as a denial.
    error.message =
        "You are not a staff of this service";  // Explain exactly why
                                                // access was denied.
    return false;                               // Deny the request.
  }

  return ((entityStaffPermissions->power & powerlevel) !=
          PowerLevel::NONE);  // AND the staff mask with the required bit;
                              // non-zero means they have it.
}
/** @brief Compares the requester id with `owner_id` in the permission JSON. */
bool PermissionManagerPrivate::isOwnerOfService(  // Definition of the owner-id
                                                  // comparison (signature
                                                  // continues next line).
    const Requester&                     requester,
    const std::optional<jsoncons::json>& permissions_j,
    const std::string&                   service_name,
    Http::Error& error)  // Rest of the signature: caller, permission document,
                         // service name and error output.
{
  if (!permissions_j.has_value() ||
      permissions_j->empty())  // No permission document means we cannot
                               // decide; treat as a bad request.
  {
    error.message =
        "Failed get service permissions for." +
        service_name;  // Say which service's permissions could not be read.
    error.code = api::v2::Http::BAD_REQUEST;  // 400 Bad Request: the lookup
                                              // produced no usable data.
    return false;                             // Deny the request.
  }

  if (!requester.idMatch(permissions_j.value()["owner_id"]
                             .as<uint64_t>()))  // Compare the caller's id with
                                                // the stored owner_id.
  {
    error.code = Http::Status::FORBIDDEN;  // 403 Forbidden: wrong identity
                                           // for this operation.
    error.message +=
        "You are not the owner of " +
        service_name;  // Append which service the caller does not own.
    return false;      // Deny the request.
  }
  return true;  // First door succeeded, so allow.
}

/** @brief Compares the requester id with `admin_id` in the permission JSON. */
bool PermissionManagerPrivate::isAdminOfService(  // Definition of the admin-id
                                                  // comparison (signature
                                                  // continues next line).
    const Requester&                     requester,
    const std::optional<jsoncons::json>& permissions_j,
    const std::string&                   service_name,
    Http::Error& error)  // Rest of the signature: caller, permission document,
                         // service name and error output.
{
  if (!permissions_j.has_value() ||
      permissions_j->empty())  // No permission document means we cannot
                               // decide; treat as a bad request.
  {
    error.message =
        "Failed get service permissions for." +
        service_name;  // Say which service's permissions could not be read.
    error.code = api::v2::Http::BAD_REQUEST;  // 400 Bad Request: the lookup
                                              // produced no usable data.
    return false;                             // Deny the request.
  }

  if (!requester.idMatch(permissions_j.value()["admin_id"]
                             .as<uint64_t>()))  // Compare the caller's id with
                                                // the stored admin_id.
  {
    error.code = Http::Status::FORBIDDEN;  // 403 Forbidden: wrong identity
                                           // for this operation.
    error.message += "You are not the admin of " +
                     service_name;  // Append which service the caller does
                                    // not administer.
    return false;                   // Deny the request.
  }
  return true;  // First door succeeded, so allow.
}
/**
 * @brief Scans the `staff` map for a `"<id>:<mask>"` entry belonging to the
 * requester.
 * @details Malformed numeric entries throw and are reported as FORBIDDEN; a
 * non-match also leaves FORBIDDEN in @p error. The mask is decoded into
 * PowerLevel bits.
 */
std::optional<Permissions::StaffPermission>
PermissionManagerPrivate::isStaffOfService(  // Definition of the staff lookup
                                             // (signature continues next line).
    const Requester&                     requester,
    const std::optional<jsoncons::json>& permissions_j,
    const std::string&                   service_name,
    Http::Error& error)  // Rest of the signature: caller, permission document,
                         // service name and error output.
{
  try  // JSON access and number parsing can throw, so guard them.
  {
    if (!permissions_j.has_value() ||
        permissions_j->empty())  // No permission document means we cannot
                                 // decide; treat as a bad request.
    {
      error.message = "Failed get service permissions for." +
                      service_name;  // Say which service's permissions
                                     // could not be read.
      error.code = api::v2::Http::BAD_REQUEST;  // 400 Bad Request: the lookup
                                                // produced no usable data.
      return std::nullopt;  // No usable staff entry; caller should treat
                            // this as denial.
    }

    const auto& staff =
        permissions_j.value()["staff"];  // Grab the 'staff' object from the
                                         // permission document.

    for (const auto& [role, members] :
         staff.object_range())  // Walk every role; structured binding gives
                                // the role name and its member list.
    {
      for (const auto& member :
           members.array_range())  // Walk each member string inside that
                                   // role's list.
      {
        if (!member.as<std::string>().empty())  // Skip blank entries so they
                                                // cannot cause parse errors.
        {
          auto raw_str =
              member.as<std::string>();    // The entry is stored as text
                                           // in the form '<id>:<mask>'.
          size_t pos = raw_str.find(':');  // Locate the colon that separates
                                           // the id from the mask.
          std::string left =
              raw_str.substr(0,
                             pos);  // Everything before the colon is the staff
                                    // id as text.
          std::string right =
              raw_str.substr(pos + 1);  // Everything after the colon is the
                                        // permission mask as text.
          if (left == std::to_string(
                          requester.getId()))  // Is this entry for the caller?
                                               // Compare the id strings.
          {
            return std::optional<Permissions::StaffPermission>(
                Permissions::StaffPermission{// Found a match: build the result
                                             // object
                                             // describing this staff member.
                                             .staff_id = static_cast<uint64_t>(
                                                 std::stoull(left)),
                                             .power = static_cast<PowerLevel>(
                                                 std::stoi(right)),
                                             .isMember =
                                                 true});  // Convert id and mask
                                                          // strings to
            // numbers, and flag this as a real
            // match.
          }
        }
      }
    }
  }
  catch (const std::exception& e)  // Runs when a staff entry is malformed
                                   // (bad number or missing colon).
  {
    error.code = Http::Status::FORBIDDEN;  // 403 Forbidden: malformed data
                                           // is treated as a denial.
    error.message =
        std::string("Invalid staff permission: ") +
        e.what();  // Report the parsing problem in the denial message.
  }

  error.code = Http::Status::FORBIDDEN;  // 403 Forbidden: malformed data is
                                         // treated as a denial.
  error.message =
      "Failed to find the user permission level in service " +
      service_name;     // Reached the end with no match: explain that too.
  return std::nullopt;  // No usable staff entry; caller should treat this as
                        // denial.
}

/** @brief Allows access only when both the requester's group and id match the
 * target. */
bool PermissionManagerPrivate::assert_group_id_match(
    const Requester&   requester,
    const std::string& groupname,
    uint64_t           client_id,
    Http::Error& error)  // Definition of the group+id check for client records.
{
  if (!requester.groupMatch(groupname) ||
      !requester.idMatch(
          client_id))  // Both must match; either mismatch means the caller is
                       // touching someone else's record.
  {
    error.code = Http::Status::FORBIDDEN;  // 403 Forbidden: malformed data
                                           // is treated as a denial.
    error.message =
        "You are not allowed to manage client with id: " +
        std::to_string(client_id) + " and group: " +
        groupname;  // Say exactly which client and group were rejected.
    return false;   // Deny the request.
  }

  return true;  // First door succeeded, so allow.
}
/**
 * Validates service creation: payload present, ids extractable, caller is the
 * provider. error: BAD_REQUEST with a reason on failure.
 */
bool api::v2::PermissionManagerPrivate::preServiceCreateChecks(
    const Requester&                     requester,
    const std::optional<jsoncons::json>& service_j,
    Http::Error& error)  // Definition of the service-creation validation.
{
  if (!service_j.has_value())  // A creation request without a body cannot be
                               // validated.
  {
    error.code    = Http::Status::BAD_REQUEST;  // 400 Bad Request: the payload
                                                // or id was missing/invalid.
    error.message = "Service data is not provided.";  // Explain that the
                                                      // payload was missing.
    return false;                                     // Deny the request.
  }

  uint64_t owner_id =
      0;  // Will hold the owner id parsed from the payload; starts at zero.
  uint64_t admin_id =
      0;  // Will hold the admin id parsed from the payload; starts at zero.

  try  // JSON access and number parsing can throw, so guard them.
  {
    owner_id =
        service_j->at("owner_id").as<uint64_t>();  // Read the initial owner
                                                   // id from the JSON body.
    admin_id =
        service_j->at("admin_id").as<uint64_t>();  // Read the initial admin
                                                   // id from the JSON body.
  }
  catch (const std::exception& e)  // Runs when a staff entry is malformed
                                   // (bad number or missing colon).
  {
    error.code = Http::Status::BAD_REQUEST;  // 400 Bad Request: the payload
                                             // or id was missing/invalid.
    error.message =
        std::string("failed to extract id ") +
        e.what();  // Report which id could not be read or converted.
    return false;  // Deny the request.
  }

  if (!requester.idMatch(owner_id) ||
      !requester.idMatch(admin_id))  // The caller must be both the initial
                                     // owner and admin of the new service.
  {
    error.code = Http::Status::BAD_REQUEST;  // 400 Bad Request: the payload
                                             // or id was missing/invalid.
    error.message =
        "Initial service admin_id and owner_id should be equal to that of "
        "provider_id";  // Explain the rule that was broken.
    return false;       // Deny the request.
  }

  if (!requester
           .isProvider())  // Only providers are allowed to create a service.
  {
    error.code = Http::Status::BAD_REQUEST;  // 400 Bad Request: the payload
                                             // or id was missing/invalid.
    error.message =
        // Tell the caller why creation is denied.
        "You are not allowed to create a service, you are not a provider.";
    return false;  // Deny the request.
  }

  return true;  // First door succeeded, so allow.
}

/**
 * @brief Allows owner/admin, or staff with the @p required capability bit.
 * @details On failure it augments @p error.message with the service name and
 * the more specific message left by hasPermission().
 */
bool api::v2::PermissionManagerPrivate::isOwnerOrAdminOrHasPermission(
    const Requester& requester,
    const std::optional<jsoncons::json>&
        permissions_j,  // Definition of the combined rule (signature continues
                        // next line).
    const std::string& service_name,
    Http::Error&       error,
    Permissions::PowerLevel
        required)  // Rest of the signature: service name, error output and the
                   // required capability bit.
{
  if (isOwnerOrAdmin(requester,
                     permissions_j,
                     service_name,
                     error))  // First door: owner or admin passes immediately.
  {
    return true;  // First door succeeded, so allow.
  }

  if (!hasPermission(requester,
                     permissions_j,
                     required,
                     service_name,
                     error))  // Second door: staff must have the required
                              // capability bit.
  {
    error.code = Http::Status::FORBIDDEN;  // 403 Forbidden: malformed data
                                           // is treated as a denial.
    error.message =
        fmt::format("You don't have the permission to manage  {} {}",
                    service_name,
                    error.message);  // Add the service name and the detailed
                                     // reason to the message.
    return false;                    // Deny the request.
  }
  return true;  // First door succeeded, so allow.
}

/**
 * Allows either the owner or the admin (short-circuits on owner success).
 * error: May still hold a message from a failed first check when the second
 * succeeds.
 */
bool api::v2::PermissionManagerPrivate::
    isOwnerOrAdmin(  // Definition of the owner-or-admin rule (signature
                     // continues next line).
        const Requester&                     requester,
        const std::optional<jsoncons::json>& permissions_j,
        const std::string&                   service_name,
        Http::Error& error)  // Rest of the signature: caller, permission
                             // document, service name and error output.
{
  return (isOwnerOfService(requester, permissions_j, service_name, error) ||
          isAdminOfService(requester,
                           permissions_j,
                           service_name,
                           error));  // OR means the owner check short-circuits
                                     // the admin check on success.
}
