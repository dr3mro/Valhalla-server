/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file permissionmanager.cpp
 * @brief The implementations of all the permission-check methods.
 *        (Originally: "Implementation for permissionmanager: Client creation is
 * unrestricted; see PermissionManager::canCreate.")
 * @details Each method here follows the same recipe: work out the service or
 *          table name, fetch that entity's permission JSON (through the private
 *          helpers), then ask the helper that matches the required right. The
 *          `Async` versions do the same using coroutines.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is where the actual allow/deny decisions are executed for every route.
 * Controllers call these methods before touching data. The bottom of the file
 * explicitly builds the concrete template versions the linker needs. Without
 * it, no request would be authorized.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Explicit template instantiation:
 * https://en.cppreference.com/w/cpp/language/template_instantiation
 * - Coroutines (co_await / co_return):
 * https://en.cppreference.com/w/cpp/language/coroutines
 * - Preprocessor macros: https://en.cppreference.com/w/cpp/preprocessor
 * - The facade pattern: https://en.wikipedia.org/wiki/Facade_pattern
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// The declarations implemented here.
#include "gatekeeper/permissionmanager/permissionmanager.hpp"

#include <fmt/core.h>  // fmt::format, used to build denial messages.

#include <cstdint>  // uint64_t for entity ids.
// jsoncons::json for permission data and payloads.
#include <jsoncons/basic_json.hpp>
#include <optional>  // std::optional for absent payloads/results.
#include <string>    // std::string for names and messages.

#include "drogon/utils/coroutine.h"  // drogon::Task, the coroutine return type.
// IWYU pragma: keep  (brings in the entity types used by the macros).
#include "gatekeeper/includes.hpp"
// The helper functions that do the real work.
#include "gatekeeper/permissionmanager/permissionmanager_private.hpp"
// Client_t, Service_t, Case_t, Appointment_t constraints.
#include "utils/global/concepts.hpp"
#include "utils/global/http.hpp"       // Http::Error for reporting denials.
#include "utils/global/requester.hpp"  // Requester, the caller being checked.
using pm_priv =
    api::v2::PermissionManagerPrivate;  // Short alias so the long qualified
                                        // name stays readable.
using PermissionManager =
    api::v2::PermissionManager;  // Short alias for the class whose methods are
                                 // defined below.
using HttpError =
    api::v2::Http::Error;  // Short alias for the error type used in signatures.
/** @brief Client creation is unrestricted; see PermissionManager::canCreate. */
template <Client_t T>  // Restricts this overload to client entity types.
bool PermissionManager::canCreate(  // Synchronous create check definition
                                    // (parameters continue on the next line).
    [[maybe_unused]] const Requester&                     requester,
    [[maybe_unused]] const std::optional<jsoncons::json>& data_j,
    [[maybe_unused]] Http::Error&
        error)  // These arguments are unused here; the marker silences compiler
                // warnings.
{
  return true;  // Allow the action; creating a client is open to everyone.
}

/** @brief Allows a client read only when group and id match the target client.
 */
template <Client_t T>  // Restricts this overload to client entity types.
bool PermissionManager::canRead(const Requester& requester, uint64_t client_id,
    HttpError& error)  // Synchronous read check for a client record.
{
  return pm_priv::assert_group_id_match(requester, T::getTableName(), client_id,
      error);  // Delegate to the group+id check: one client cannot manage
               // another.
}
/** @brief Allows a client update only when group and id match the target
 * client. */
template <Client_t T>  // Restricts this overload to client entity types.
bool PermissionManager::canUpdate(const Requester& requester,
    uint64_t                                       client_id,
    HttpError& error)  // Synchronous update check for a client record.
{
  return pm_priv::assert_group_id_match(requester, T::getTableName(), client_id,
      error);  // Delegate to the group+id check: one client cannot manage
               // another.
}
/** @brief Allows a client deletion only when group and id match the target
 * client. */
template <Client_t T>  // Restricts this overload to client entity types.
bool PermissionManager::canDelete(const Requester& requester,
    const uint64_t                                 client_id,
    HttpError& error)  // Synchronous delete check for a client record.
{
  return pm_priv::assert_group_id_match(requester, T::getTableName(), client_id,
      error);  // Delegate to the group+id check: one client cannot manage
               // another.
}

/** @brief Allows toggling a client's active flag only on a matching group and
 * id. */
template <Client_t T>  // Restricts this overload to client entity types.
bool PermissionManager::canToggleActive(const Requester& requester,
    const uint64_t                                       client_id,
    Http::Error&
        error)  // Synchronous check for changing a client's active flag.
{
  return pm_priv::assert_group_id_match(requester, T::getTableName(), client_id,
      error);  // Delegate to the group+id check: one client cannot manage
               // another.
}

/** @brief Allows listing a client's services only on a matching group and id.
 */
template <Client_t T>  // Restricts this overload to client entity types.
bool PermissionManager::canGetServices(const Requester& requester,
    const uint64_t                                      client_id,
    Http::Error& error)  // Synchronous check for listing a client's services.
{
  return pm_priv::assert_group_id_match(requester, T::getTableName(), client_id,
      error);  // Delegate to the group+id check: one client cannot manage
               // another.
}

/** @brief Delegates service creation to the owner/admin preconditions on the
 * payload. */
template <Service_t T>  // Restricts this overload to service entity types.
bool PermissionManager::canCreate(const Requester& requester,
    const std::optional<jsoncons::json>&           service_j,
    Http::Error& error)  // Synchronous create check; payload identifies the
                         // target service.
{
  return pm_priv::preServiceCreateChecks(requester, service_j,
      error);  // Delegate to the payload/caller validation helper.
}

/** @brief Allows service reads for owner, admin, or staff with CAN_READ. */
template <Service_t T>  // Restricts this overload to service entity types.
bool PermissionManager::canRead(const Requester& requester, uint64_t service_id,
    Http::Error& error)  // Synchronous read check for a service or appointment.
{
  const std::string service_name =
      T::getTableName();  // Table name, used in the query and in denial
                          // messages.

  std::optional<jsoncons::json> permissions_j = pm_priv::getPermissionsOfEntity(
      T::getServicePermissionsQuery, service_name,
      service_id);  // Load the service's permission JSON (cache first,
                    // database on a miss).

  return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j,
      service_name, error,
      Permissions::PowerLevel::CAN_READ);  // Owner/admin pass; staff need the
                                           // CAN_READ bit.
}

/** @brief Allows service updates for owner, admin, or staff with CAN_WRITE. */
template <Service_t T>  // Restricts this overload to service entity types.
bool PermissionManager::canUpdate(const Requester& requester,
    uint64_t                                       service_id,
    Http::Error&
        error)  // Synchronous update check for a service or appointment.
{
  const std::string service_name =
      T::getTableName();  // Table name, used in the query and in denial
                          // messages.

  std::optional<jsoncons::json> permissions_j = pm_priv::getPermissionsOfEntity(
      T::getServicePermissionsQuery, service_name,
      service_id);  // Load the service's permission JSON (cache first,
                    // database on a miss).

  return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j,
      service_name, error,
      Permissions::PowerLevel::CAN_WRITE);  // Owner/admin pass; staff need
                                            // the CAN_WRITE bit.
}

/** @brief Restricts service deletion to the owner (admins/staff are not
 * enough). */
template <Service_t T>  // Restricts this overload to service entity types.
bool PermissionManager::canDelete(const Requester& requester,
    const uint64_t                                 service_id,
    Http::Error& error)  // Synchronous delete check for a service.
{
  const std::string service_name =
      T::getTableName();  // Table name, used in the query and in denial
                          // messages.

  std::optional<jsoncons::json> permissions_j = pm_priv::getPermissionsOfEntity(
      T::getServicePermissionsQuery, service_name,
      service_id);  // Load the service's permission JSON (cache first,
                    // database on a miss).

  return pm_priv::isOwnerOfService(requester, permissions_j, service_name,
      error);  // Deletion is restricted to the owner only.
}

/** @brief Restricts staff management to the service owner or admin. */
template <typename T>  // Generic overload; T must provide the table/query
                       // helpers used inside.
bool PermissionManager::canManageStaff(const Requester& requester,
    uint64_t                                            service_id,
    Http::Error& error)  // Synchronous check; only
                         // owner/admin may manage staff.
{
  std::string service_name =
      T::getTableName();  // Table name, used in the query and in denial
                          // messages.

  std::optional<jsoncons::json> permissions_j = pm_priv::getPermissionsOfEntity(
      T::getServicePermissionsQuery, service_name,
      service_id);  // Load the service's permission JSON (cache first,
                    // database on a miss).

  return pm_priv::isOwnerOrAdmin(requester, permissions_j, service_name,
      error);  // Only the owner or admin may manage staff.
}

/** @brief Allows case creation for owner/admin or staff with CAN_WRITE; query
 * derived from payload. */
template <Case_t T>  // Restricts this overload to case entity types.
bool PermissionManager::canCreate(const Requester& requester,
    const std::optional<jsoncons::json>&           data_j,
    Http::Error& error)  // Synchronous create check; payload identifies the
                         // parent service.
{
  std::string service_name =
      T::getTableName();  // Table name, used in the query and in denial
                          // messages.

  std::optional<jsoncons::json> permissions_j =
      pm_priv::getPermissionsOfEntity(T::getPermissionsQueryForCreate,
          data_j);  // Build the permission query from the creation payload
                    // and load it.

  return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j,
      service_name, error,
      Permissions::PowerLevel::CAN_WRITE);  // Owner/admin pass; staff need
                                            // the CAN_WRITE bit.
}

/** @brief Allows case reads for owner/admin or staff with CAN_READ. */
template <Case_t T>  // Restricts this overload to case entity types.
bool PermissionManager::canRead(const Requester& requester, const uint64_t _id,
    Http::Error& error)  // Synchronous read check; _id is the target entity id.
{
  std::string service_name =
      T::getTableName();  // Table name, used in the query and in denial
                          // messages.

  std::optional<jsoncons::json> permissions_j =
      pm_priv::getPermissionsOfEntity(T::getPermissionsQueryForRead,
          _id);  // Build the read-permission query from the entity id and
                 // load it.

  return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j,
      service_name, error,
      Permissions::PowerLevel::CAN_READ);  // Owner/admin pass; staff need the
                                           // CAN_READ bit.
}

/** @brief Allows case updates for owner/admin or staff with CAN_WRITE. */
template <Case_t T>  // Restricts this overload to case entity types.
bool PermissionManager::canUpdate(const Requester& requester,
    const uint64_t                                 _id,
    Http::Error&
        error)  // Synchronous update check; _id is the target entity id.
{
  std::string service_name =
      T::getTableName();  // Table name, used in the query and in denial
                          // messages.

  std::optional<jsoncons::json> permissions_j =
      pm_priv::getPermissionsOfEntity(&T::getPermissionsQueryForUpdate,
          _id);  // Build the update-permission query from the entity id and
                 // load it.

  return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j,
      service_name, error,
      Permissions::PowerLevel::CAN_WRITE);  // Owner/admin pass; staff need
                                            // the CAN_WRITE bit.
}

/** @brief Allows case deletion for owner/admin or staff with CAN_DELETE. */
template <Case_t T>  // Restricts this overload to case entity types.
bool PermissionManager::canDelete(const Requester& requester,
    const uint64_t                                 _id,
    Http::Error&
        error)  // Synchronous delete check; _id is the target entity id.
{
  std::string service_name =
      T::getTableName();  // Table name, used in the query and in denial
                          // messages.
  std::optional<jsoncons::json> permissions_j =
      pm_priv::getPermissionsOfEntity(&T::getPermissionsQueryForDelete,
          _id);  // Build the delete-permission query from the entity id and
                 // load it.

  return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j,
      service_name, error,
      Permissions::PowerLevel::CAN_DELETE);  // Owner/admin pass; staff need
                                             // the CAN_DELETE bit.
}

/** @brief Allows appointment creation for owner/admin or staff with CAN_WRITE;
 * needs a clinic id in the payload. */
template <
    Appointment_t T>  // Restricts this overload to appointment entity types.
bool PermissionManager::canCreate(const Requester& requester,
    const std::optional<jsoncons::json>&           service_j,
    Http::Error& error)  // Synchronous create check; payload identifies the
                         // target service.
{
  std::string service_name =
      T::getOrgName();  // Organisation name for appointment types, used in
                        // messages/queries.

  auto service_id = pm_priv::extract_json_value_safely<uint64_t>(service_j,
      "clinic_id", service_name,
      error);  // Pull the target clinic id out of the payload; failure fills
               // in error.

  if (!service_id.has_value())  // If the id could not be extracted, deny
                                // without a permission lookup.
  {
    return false;  // Deny the request.
  }
  std::optional<jsoncons::json> permissions_j = pm_priv::getPermissionsOfEntity(
      T::getServicePermissionsQuery, service_name,
      service_id.value());  // Appointment create uses the extracted
                            // clinic id to load permissions.

  return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j,
      service_name, error,
      Permissions::PowerLevel::CAN_WRITE);  // Owner/admin pass; staff need
                                            // the CAN_WRITE bit.
}

/** @brief Allows appointment reads for owner/admin or staff with CAN_READ. */
template <
    Appointment_t T>  // Restricts this overload to appointment entity types.
bool PermissionManager::canRead(const Requester& requester,
    uint64_t                                     appointment_id,
    Http::Error& error)  // Synchronous read check for an appointment.
{
  std::string service_name =
      T::getOrgName();  // Organisation name for appointment types, used in
                        // messages/queries.

  std::optional<jsoncons::json> permissions_j =
      pm_priv::getPermissionsOfEntity(T::getPermissionsQueryForRead,
          appointment_id);  // Load permissions for the appointment's parent
                            // service.

  return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j,
      service_name, error,
      Permissions::PowerLevel::CAN_READ);  // Owner/admin pass; staff need the
                                           // CAN_READ bit.
}

/** @brief Allows appointment updates for owner/admin or staff with CAN_WRITE.
 */
template <
    Appointment_t T>  // Restricts this overload to appointment entity types.
bool PermissionManager::canUpdate(const Requester& requester,
    const uint64_t                                 appointment_id,
    Http::Error& error)  // Synchronous update check for an appointment.
{
  std::string service_name =
      T::getOrgName();  // Organisation name for appointment types, used in
                        // messages/queries.

  std::optional<jsoncons::json> permissions_j =
      pm_priv::getPermissionsOfEntity(T::getPermissionsQueryForRead,
          appointment_id);  // Load permissions for the appointment's parent
                            // service.

  return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j,
      service_name, error,
      Permissions::PowerLevel::CAN_WRITE);  // Owner/admin pass; staff need
                                            // the CAN_WRITE bit.
}

/** @brief Allows appointment deletion for owner/admin or staff with CAN_DELETE.
 */
template <
    Appointment_t T>  // Restricts this overload to appointment entity types.
bool PermissionManager::canDelete(const Requester& requester,
    uint64_t                                       appointment_id,
    Http::Error& error)  // Synchronous delete check for an appointment.
{
  std::string service_name =
      T::getOrgName();  // Organisation name for appointment types, used in
                        // messages/queries.

  std::optional<jsoncons::json> permissions_j =
      pm_priv::getPermissionsOfEntity(T::getPermissionsQueryForRead,
          appointment_id);  // Load permissions for the appointment's parent
                            // service.

  return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j,
      service_name, error,
      Permissions::PowerLevel::CAN_DELETE);  // Owner/admin pass; staff need
                                             // the CAN_DELETE bit.
}

/** @brief Coroutine form of client canCreate(); always returns `true`. */
template <Client_t T>  // Restricts this overload to client entity types.
drogon::Task<bool>
PermissionManager::canCreateAsync(  // Coroutine create check definition.
    [[maybe_unused]] const Requester&                     requester,
    [[maybe_unused]] const std::optional<jsoncons::json>& data_j,
    [[maybe_unused]] Http::Error&
        error)  // These arguments are unused here; the marker silences compiler
                // warnings.
{
  co_return true;  // Coroutine return: allow the action.
}

/** @brief Coroutine form of client canRead(); group and id must match. */
template <Client_t T>  // Restricts this overload to client entity types.
drogon::Task<bool> PermissionManager::canReadAsync(const Requester& requester,
    uint64_t                                                        client_id,
    HttpError& error)  // Coroutine read check for a client record.
{
  co_return pm_priv::assert_group_id_match(requester, T::getTableName(),
      client_id, error);  // Coroutine return of the group+id check result.
}
/** @brief Coroutine form of client canUpdate(); group and id must match. */
template <Client_t T>  // Restricts this overload to client entity types.
drogon::Task<bool> PermissionManager::canUpdateAsync(const Requester& requester,
    uint64_t                                                          client_id,
    HttpError& error)  // Coroutine update check for a client record.
{
  co_return pm_priv::assert_group_id_match(requester, T::getTableName(),
      client_id, error);  // Coroutine return of the group+id check result.
}
/** @brief Coroutine form of client canDelete(); group and id must match. */
template <Client_t T>  // Restricts this overload to client entity types.
drogon::Task<bool> PermissionManager::canDeleteAsync(const Requester& requester,
    const uint64_t                                                    client_id,
    HttpError& error)  // Coroutine delete check for a client record.
{
  co_return pm_priv::assert_group_id_match(requester, T::getTableName(),
      client_id, error);  // Coroutine return of the group+id check result.
}

/** @brief Coroutine form of client canToggleActive(); group and id must match.
 */
template <Client_t T>  // Restricts this overload to client entity types.
drogon::Task<bool> PermissionManager::canToggleActiveAsync(
    const Requester& requester, const uint64_t client_id,
    Http::Error& error)  // Coroutine check for changing a client's active flag.
{
  co_return pm_priv::assert_group_id_match(requester, T::getTableName(),
      client_id, error);  // Coroutine return of the group+id check result.
}

/** @brief Coroutine form of client canGetServices(); group and id must match.
 */
template <Client_t T>  // Restricts this overload to client entity types.
drogon::Task<bool> PermissionManager::canGetServicesAsync(
    const Requester& requester, const uint64_t client_id,
    Http::Error& error)  // Coroutine check for listing a client's services.
{
  co_return pm_priv::assert_group_id_match(requester, T::getTableName(),
      client_id, error);  // Coroutine return of the group+id check result.
}

/** @brief Coroutine form of service canCreate(); checks the owner/admin
 * preconditions. */
template <Service_t T>  // Restricts this overload to service entity types.
drogon::Task<bool> PermissionManager::canCreateAsync(const Requester& requester,
    const std::optional<jsoncons::json>&                              service_j,
    Http::Error& error)  // Coroutine create check using the service payload.
{
  co_return pm_priv::preServiceCreateChecks(requester, service_j,
      error);  // Coroutine return of the creation preconditions.
}

/** @brief Coroutine form of service canRead(); owner/admin or CAN_READ staff.
 */
template <Service_t T>  // Restricts this overload to service entity types.
drogon::Task<bool> PermissionManager::canReadAsync(const Requester& requester,
    uint64_t                                                        service_id,
    Http::Error& error)  // Coroutine read check for a service or appointment.
{
  const std::string service_name =
      T::getTableName();  // Table name, used in the query and in denial
                          // messages.
  std::optional<jsoncons::json> permissions_j =
      co_await pm_priv::getPermissionsOfEntityCoro(
          T::getServicePermissionsQuery, service_name,
          service_id);  // Coroutine load of the service's permission JSON
                        // without blocking an IO thread.

  co_return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j,
      service_name, error,
      Permissions::PowerLevel::CAN_READ);  // Coroutine return: owner/admin or
                                           // staff with CAN_READ.
}

/** @brief Coroutine form of service canUpdate(); owner/admin or CAN_WRITE
 * staff. */
template <Service_t T>  // Restricts this overload to service entity types.
drogon::Task<bool> PermissionManager::canUpdateAsync(const Requester& requester,
    uint64_t     service_id,
    Http::Error& error)  // Coroutine update check for a service or appointment.
{
  const std::string service_name =
      T::getTableName();  // Table name, used in the query and in denial
                          // messages.
  std::optional<jsoncons::json> permissions_j =
      co_await pm_priv::getPermissionsOfEntityCoro(
          T::getServicePermissionsQuery, service_name,
          service_id);  // Coroutine load of the service's permission JSON
                        // without blocking an IO thread.

  co_return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j,
      service_name, error,
      Permissions::PowerLevel::CAN_WRITE);  // Coroutine return: owner/admin
                                            // or staff with CAN_WRITE.
}

/** @brief Coroutine form of service canDelete(); owner only. */
template <Service_t T>  // Restricts this overload to service entity types.
drogon::Task<bool> PermissionManager::canDeleteAsync(const Requester& requester,
    const uint64_t service_id,
    Http::Error&   error)  // Coroutine delete check for a service.
{
  const std::string service_name =
      T::getTableName();  // Table name, used in the query and in denial
                          // messages.
  std::optional<jsoncons::json> permissions_j =
      co_await pm_priv::getPermissionsOfEntityCoro(
          T::getServicePermissionsQuery, service_name,
          service_id);  // Coroutine load of the service's permission JSON
                        // without blocking an IO thread.

  co_return pm_priv::isOwnerOfService(requester, permissions_j, service_name,
      error);  // Coroutine return: owner-only decision.
}

/** @brief Coroutine form of canManageStaff(); owner or admin only. */
template <
    typename T>  // Generic overload; T must provide the table/query helpers
                 // used inside.
drogon::Task<bool> PermissionManager::canManageStaffAsync(
    const Requester& requester, uint64_t service_id,
    Http::Error& error)  // Coroutine staff-management check.
{
  std::string service_name =
      T::getTableName();  // Table name, used in the query and in denial
                          // messages.
  std::optional<jsoncons::json> permissions_j =
      co_await pm_priv::getPermissionsOfEntityCoro(
          T::getServicePermissionsQuery, service_name,
          service_id);  // Coroutine load of the service's permission JSON
                        // without blocking an IO thread.

  co_return pm_priv::isOwnerOrAdmin(requester, permissions_j, service_name,
      error);  // Coroutine return: owner-or-admin decision.
}

/** @brief Coroutine form of case canCreate(); owner/admin or CAN_WRITE staff.
 */
template <Case_t T>  // Restricts this overload to case entity types.
drogon::Task<bool> PermissionManager::canCreateAsync(const Requester& requester,
    const std::optional<jsoncons::json>&                              data_j,
    Http::Error&
        error)  // Coroutine create check using the client/case payload.
{
  std::string service_name =
      T::getTableName();  // Table name, used in the query and in denial
                          // messages.
  std::optional<jsoncons::json> permissions_j =
      co_await pm_priv::getPermissionsOfEntityCoro(
          T::getPermissionsQueryForCreate,
          data_j);  // Coroutine load using a query built from the payload.

  co_return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j,
      service_name, error,
      Permissions::PowerLevel::CAN_WRITE);  // Coroutine return: owner/admin
                                            // or staff with CAN_WRITE.
}

/** @brief Coroutine form of case canRead(); owner/admin or CAN_READ staff. */
template <Case_t T>  // Restricts this overload to case entity types.
drogon::Task<bool> PermissionManager::canReadAsync(const Requester& requester,
    const uint64_t                                                  _id,
    Http::Error& error)  // Coroutine read check; _id is the target entity id.
{
  std::string service_name =
      T::getTableName();  // Table name, used in the query and in denial
                          // messages.
  std::optional<jsoncons::json> permissions_j =
      co_await pm_priv::getPermissionsOfEntityCoro(
          T::getPermissionsQueryForRead,
          _id);  // Coroutine load using a query built from the id.

  co_return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j,
      service_name, error,
      Permissions::PowerLevel::CAN_READ);  // Coroutine return: owner/admin or
                                           // staff with CAN_READ.
}

/** @brief Coroutine form of case canUpdate(); owner/admin or CAN_WRITE staff.
 */
template <Case_t T>  // Restricts this overload to case entity types.
drogon::Task<bool> PermissionManager::canUpdateAsync(const Requester& requester,
    const uint64_t                                                    _id,
    Http::Error& error)  // Coroutine update check; _id is the target entity id.
{
  std::string service_name =
      T::getTableName();  // Table name, used in the query and in denial
                          // messages.
  std::optional<jsoncons::json> permissions_j =
      co_await pm_priv::getPermissionsOfEntityCoro(
          &T::getPermissionsQueryForUpdate,
          _id);  // Coroutine load using the update query.

  co_return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j,
      service_name, error,
      Permissions::PowerLevel::CAN_WRITE);  // Coroutine return: owner/admin
                                            // or staff with CAN_WRITE.
}

/** @brief Coroutine form of case canDelete(); owner/admin or CAN_DELETE staff.
 */
template <Case_t T>  // Restricts this overload to case entity types.
drogon::Task<bool> PermissionManager::canDeleteAsync(const Requester& requester,
    const uint64_t                                                    _id,
    Http::Error& error)  // Coroutine delete check; _id is the target entity id.
{
  std::string service_name =
      T::getTableName();  // Table name, used in the query and in denial
                          // messages.
  std::optional<jsoncons::json> permissions_j =
      co_await pm_priv::getPermissionsOfEntityCoro(
          &T::getPermissionsQueryForDelete,
          _id);  // Coroutine load using the delete query.

  co_return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j,
      service_name, error,
      Permissions::PowerLevel::CAN_DELETE);  // Coroutine return: owner/admin
                                             // or staff with CAN_DELETE.
}

/** @brief Coroutine form of appointment canCreate(); needs a clinic id in the
 * payload. */
template <
    Appointment_t T>  // Restricts this overload to appointment entity types.
drogon::Task<bool> PermissionManager::canCreateAsync(const Requester& requester,
    const std::optional<jsoncons::json>&                              service_j,
    Http::Error& error)  // Coroutine create check using the service payload.
{
  std::string service_name =
      T::getOrgName();  // Organisation name for appointment types, used in
                        // messages/queries.

  auto service_id = pm_priv::extract_json_value_safely<uint64_t>(service_j,
      "clinic_id", service_name,
      error);  // Pull the target clinic id out of the payload; failure fills
               // in error.

  if (!service_id.has_value())  // If the id could not be extracted, deny
                                // without a permission lookup.
  {
    co_return false;  // Coroutine return: deny the request.
  }
  std::optional<jsoncons::json> permissions_j =
      co_await pm_priv::getPermissionsOfEntityCoro(
          T::getServicePermissionsQuery, service_name,
          service_id.value());  // Coroutine load using the extracted clinic id.

  co_return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j,
      service_name, error,
      Permissions::PowerLevel::CAN_WRITE);  // Coroutine return: owner/admin
                                            // or staff with CAN_WRITE.
}

/** @brief Coroutine form of appointment canRead(); owner/admin or CAN_READ
 * staff. */
template <
    Appointment_t T>  // Restricts this overload to appointment entity types.
drogon::Task<bool> PermissionManager::canReadAsync(const Requester& requester,
    uint64_t     appointment_id,
    Http::Error& error)  // Coroutine read check for an appointment.
{
  std::string service_name =
      T::getOrgName();  // Organisation name for appointment types, used in
                        // messages/queries.
  std::optional<jsoncons::json> permissions_j =
      co_await pm_priv::getPermissionsOfEntityCoro(
          T::getPermissionsQueryForRead,
          appointment_id);  // Coroutine load for the appointment's parent
                            // service.

  co_return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j,
      service_name, error,
      Permissions::PowerLevel::CAN_READ);  // Coroutine return: owner/admin or
                                           // staff with CAN_READ.
}

/** @brief Coroutine form of appointment canUpdate(); owner/admin or CAN_WRITE
 * staff. */
template <
    Appointment_t T>  // Restricts this overload to appointment entity types.
drogon::Task<bool> PermissionManager::canUpdateAsync(const Requester& requester,
    const uint64_t appointment_id,
    Http::Error&   error)  // Coroutine update check for an appointment.
{
  std::string service_name =
      T::getOrgName();  // Organisation name for appointment types, used in
                        // messages/queries.
  std::optional<jsoncons::json> permissions_j =
      co_await pm_priv::getPermissionsOfEntityCoro(
          T::getPermissionsQueryForRead,
          appointment_id);  // Coroutine load for the appointment's parent
                            // service.

  co_return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j,
      service_name, error,
      Permissions::PowerLevel::CAN_WRITE);  // Coroutine return: owner/admin
                                            // or staff with CAN_WRITE.
}

/** @brief Coroutine form of appointment canDelete(); owner/admin or CAN_DELETE
 * staff. */
template <
    Appointment_t T>  // Restricts this overload to appointment entity types.
drogon::Task<bool> PermissionManager::canDeleteAsync(const Requester& requester,
    uint64_t     appointment_id,
    Http::Error& error)  // Coroutine delete check for an appointment.
{
  std::string service_name =
      T::getOrgName();  // Organisation name for appointment types, used in
                        // messages/queries.
  std::optional<jsoncons::json> permissions_j =
      co_await pm_priv::getPermissionsOfEntityCoro(
          T::getPermissionsQueryForRead,
          appointment_id);  // Coroutine load for the appointment's parent
                            // service.

  co_return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j,
      service_name, error,
      Permissions::PowerLevel::CAN_DELETE);  // Coroutine return: owner/admin
                                             // or staff with CAN_DELETE.
}

// The templates are defined in this translation unit, so every supported entity
// type must be explicitly instantiated here for the linker. The macros below
// compose the CRUD-only set with the extra operations (staff management,
// group/id checks) that some entity families need. In everyday words: a
// template is a recipe, not compiled machine code. These lines tell the
// compiler to actually bake the specificrecipe for each entity type we use. The
// trailing backslashes glue a macro's lines into one logical line. Builds the
// four synchronous CRUD checks and their four coroutine twins for TYPE.
#define INSTANTIATE_PERMISSION_CRUD(TYPE)                                  \
  template bool PermissionManager::canCreate<TYPE>(                        \
      const Requester&, const std::optional<jsoncons::json>&, HttpError&); \
  template bool PermissionManager::canRead<TYPE>(                          \
      const Requester&, const uint64_t entity_id, HttpError&);             \
  template bool PermissionManager::canUpdate<TYPE>(                        \
      const Requester&, const uint64_t entity_id, HttpError&);             \
  template bool PermissionManager::canDelete<TYPE>(                        \
      const Requester&, const uint64_t entity_id, HttpError&);             \
  template drogon::Task<bool> PermissionManager::canCreateAsync<TYPE>(     \
      const Requester&, const std::optional<jsoncons::json>&, HttpError&); \
  template drogon::Task<bool> PermissionManager::canReadAsync<TYPE>(       \
      const Requester&, const uint64_t entity_id, HttpError&);             \
  template drogon::Task<bool> PermissionManager::canUpdateAsync<TYPE>(     \
      const Requester&, const uint64_t entity_id, HttpError&);             \
  template drogon::Task<bool> PermissionManager::canDeleteAsync<TYPE>(     \
      const Requester&, const uint64_t entity_id,                          \
      HttpError&);  // Compile the coroutine delete check for TYPE.

// Adds the client-only checks (list services, toggle active) on top of CRUD.
#define INSTANTIATE_PERMISSION_CLIENT(TYPE)                                  \
  INSTANTIATE_PERMISSION_CRUD(TYPE)                                          \
  template bool PermissionManager::canGetServices<TYPE>(                     \
      const Requester& requester, const uint64_t id, HttpError& error);      \
  template bool PermissionManager::canToggleActive<TYPE>(                    \
      const Requester& requester, const uint64_t id, HttpError& error);      \
  template drogon::Task<bool> PermissionManager::canGetServicesAsync<TYPE>(  \
      const Requester& requester, const uint64_t id, HttpError& error);      \
  template drogon::Task<bool> PermissionManager::canToggleActiveAsync<TYPE>( \
      const Requester& requester, const uint64_t id,                         \
      HttpError&                                                             \
          error);  // Compile the coroutine toggle-active check for TYPE.

// Adds the staff-management checks on top of CRUD, for service-like entities.
#define INSTANTIATE_PERMISSION_ENTITY(TYPE)                                 \
  INSTANTIATE_PERMISSION_CRUD(TYPE)                                         \
  template bool PermissionManager::canManageStaff<TYPE>(                    \
      const Requester&, const uint64_t entity_id, HttpError&);              \
  template drogon::Task<bool> PermissionManager::canManageStaffAsync<TYPE>( \
      const Requester&, const uint64_t entity_id,                           \
      HttpError&);  // Compile the coroutine
                    // staff-management check for
                    // TYPE.

// Client types: a user may only manage their own client record.
INSTANTIATE_PERMISSION_CLIENT(
    User)  // Compile the concrete client checks for User.
INSTANTIATE_PERMISSION_CLIENT(
    Provider)  // Compile the concrete client checks for Provider.

// Entity types with staff management: owner/admin/staff rules apply.
INSTANTIATE_PERMISSION_ENTITY(
    Clinics)  // Compile checks (including staff) for Clinics.
INSTANTIATE_PERMISSION_ENTITY(
    Pharmacies)  // Compile checks (including staff) for Pharmacies.
INSTANTIATE_PERMISSION_ENTITY(
    Laboratories)  // Compile checks (including staff) for Laboratories.
INSTANTIATE_PERMISSION_ENTITY(
    RadiologyCenters)  // Compile checks (including staff) for RadiologyCenters.

// Regular entities with CRUD: just the create/read/update/delete checks.
INSTANTIATE_PERMISSION_CRUD(Patient)
INSTANTIATE_PERMISSION_CRUD(Health)
INSTANTIATE_PERMISSION_CRUD(PatientDrugs)
INSTANTIATE_PERMISSION_CRUD(Reports)
INSTANTIATE_PERMISSION_CRUD(Visits)
INSTANTIATE_PERMISSION_CRUD(VisitDrugs)
INSTANTIATE_PERMISSION_CRUD(Prescriptions)
INSTANTIATE_PERMISSION_CRUD(Requests)
INSTANTIATE_PERMISSION_CRUD(PaidServices)

INSTANTIATE_PERMISSION_CRUD(ClinicAppointment)
INSTANTIATE_PERMISSION_CRUD(PharmacyAppointment)
INSTANTIATE_PERMISSION_CRUD(LaboratoryAppointment)
INSTANTIATE_PERMISSION_CRUD(RadiologyCenterAppointment)
