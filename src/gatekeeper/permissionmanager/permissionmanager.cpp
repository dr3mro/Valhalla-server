#include "gatekeeper/permissionmanager/permissionmanager.hpp"

#include <fmt/core.h>

#include <cstdint>
#include <jsoncons/basic_json.hpp>
#include <optional>
#include <string>

#include "gatekeeper/includes.hpp"  // IWYU pragma: keep
#include "gatekeeper/permissionmanager/permissionmanager_private.hpp"
#include "utils/global/concepts.hpp"
#include "utils/global/http.hpp"
#include "utils/global/requester.hpp"
using pm_priv           = api::v2::PermissionManagerPrivate;
using PermissionManager = api::v2::PermissionManager;
using HttpError         = api::v2::Http::Error;
template <Client_t T>
bool PermissionManager::canCreate(
    [[maybe_unused]] const Requester& requester, [[maybe_unused]] const std::optional<jsoncons::json>& data_j, [[maybe_unused]] Http::Error& error)
{
    return true;
}

template <Client_t T>
bool PermissionManager::canRead(const Requester& requester, uint64_t client_id, HttpError& error)
{
    return pm_priv::assert_group_id_match(requester, T::getTableName(), client_id, error);
}
template <Client_t T>
bool PermissionManager::canUpdate(const Requester& requester, uint64_t client_id, HttpError& error)
{
    return pm_priv::assert_group_id_match(requester, T::getTableName(), client_id, error);
}
template <Client_t T>
bool PermissionManager::canDelete(const Requester& requester, const uint64_t client_id, HttpError& error)
{
    return pm_priv::assert_group_id_match(requester, T::getTableName(), client_id, error);
}

template <Client_t T>
bool PermissionManager::canToggleActive(const Requester& requester, const uint64_t client_id, Http::Error& error)
{
    return pm_priv::assert_group_id_match(requester, T::getTableName(), client_id, error);
}

template <Client_t T>
bool PermissionManager::canGetServices(const Requester& requester, const uint64_t client_id, Http::Error& error)
{
    return pm_priv::assert_group_id_match(requester, T::getTableName(), client_id, error);
}

template <Service_t T>
bool PermissionManager::canCreate(const Requester& requester, const std::optional<jsoncons::json>& service_j, Http::Error& error)
{
    return pm_priv::preServiceCreateChecks(requester, service_j, error);
}

template <Service_t T>
bool PermissionManager::canRead(const Requester& requester, uint64_t service_id, Http::Error& error)
{
    const std::string service_name = T::getTableName();

    std::optional<jsoncons::json> permissions_j = pm_priv::getPermissionsOfEntity(T::getServicePermissionsQuery, service_name, service_id);

    return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j, service_name, error);
}

template <Service_t T>
bool PermissionManager::canUpdate(const Requester& requester, uint64_t service_id, Http::Error& error)
{
    const std::string service_name = T::getTableName();

    std::optional<jsoncons::json> permissions_j = pm_priv::getPermissionsOfEntity(T::getServicePermissionsQuery, service_name, service_id);

    return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j, service_name, error);
}

template <Service_t T>
bool PermissionManager::canDelete(const Requester& requester, const uint64_t service_id, Http::Error& error)
{
    const std::string service_name = T::getTableName();

    std::optional<jsoncons::json> permissions_j = pm_priv::getPermissionsOfEntity(T::getServicePermissionsQuery, service_name, service_id);

    return pm_priv::isOwnerOfService(requester, permissions_j, service_name, error);
}

template <typename T>
bool PermissionManager::canManageStaff(const Requester& requester, uint64_t service_id, Http::Error& error)
{
    std::string service_name = T::getTableName();

    std::optional<jsoncons::json> permissions_j = pm_priv::getPermissionsOfEntity(T::getServicePermissionsQuery, service_name, service_id);

    return pm_priv::isOwnerOrAdmin(requester, permissions_j, service_name, error);
}

template <Case_t T>
bool PermissionManager::canCreate(const Requester& requester, const std::optional<jsoncons::json>& data_j, Http::Error& error)
{
    std::string service_name = T::getTableName();

    std::optional<jsoncons::json> permissions_j = pm_priv::getPermissionsOfEntity(T::getPermissionsQueryForCreate, data_j);

    return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j, service_name, error);
}

template <Case_t T>
bool PermissionManager::canRead(const Requester& requester, const uint64_t _id, Http::Error& error)
{
    std::string service_name = T::getTableName();

    std::optional<jsoncons::json> permissions_j = pm_priv::getPermissionsOfEntity(T::getPermissionsQueryForRead, _id);

    return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j, service_name, error);
}

template <Case_t T>
bool PermissionManager::canUpdate(const Requester& requester, const uint64_t _id, Http::Error& error)
{
    std::string service_name = T::getTableName();

    std::optional<jsoncons::json> permissions_j = pm_priv::getPermissionsOfEntity(&T::getPermissionsQueryForUpdate, _id);

    return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j, service_name, error);
}

template <Case_t T>
bool PermissionManager::canDelete(const Requester& requester, const uint64_t _id, Http::Error& error)
{
    std::string                   service_name  = T::getTableName();
    std::optional<jsoncons::json> permissions_j = pm_priv::getPermissionsOfEntity(&T::getPermissionsQueryForDelete, _id);

    return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j, service_name, error);
}

template <Appointment_t T>
bool PermissionManager::canCreate(const Requester& requester, const std::optional<jsoncons::json>& service_j, Http::Error& error)
{
    std::string service_name = T::getOrgName();

    auto service_id = pm_priv::extract_json_value_safely<uint64_t>(service_j, "clinic_id", service_name, error);

    if (!service_id.has_value())
    {
        return false;
    }
    std::optional<jsoncons::json> permissions_j = pm_priv::getPermissionsOfEntity(T::getServicePermissionsQuery, service_name, service_id.value());

    return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j, service_name, error);
}

template <Appointment_t T>
bool PermissionManager::canRead(const Requester& requester, uint64_t service_id, Http::Error& error)
{
    std::string service_name = T::getOrgName();

    std::optional<jsoncons::json> permissions_j = pm_priv::getPermissionsOfEntity(T::getServicePermissionsQuery, service_name, service_id);

    return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j, service_name, error);
}

template <Appointment_t T>
bool PermissionManager::canUpdate(const Requester& requester, const uint64_t service_id, Http::Error& error)
{
    std::string service_name = T::getOrgName();

    std::optional<jsoncons::json> permissions_j = pm_priv::getPermissionsOfEntity(T::getServicePermissionsQuery, service_name, service_id);

    return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j, service_name, error);
}

template <Appointment_t T>
bool PermissionManager::canDelete(const Requester& requester, uint64_t service_id, Http::Error& error)
{
    std::string service_name = T::getOrgName();

    std::optional<jsoncons::json> permissions_j = pm_priv::getPermissionsOfEntity(T::getServicePermissionsQuery, service_name, service_id);

    return pm_priv::isOwnerOrAdminOrHasPermission(requester, permissions_j, service_name, error);
}

#define INSTANTIATE_PERMISSION_CRUD(TYPE)                                                                                 \
    template bool PermissionManager::canCreate<TYPE>(const Requester&, const std::optional<jsoncons::json>&, HttpError&); \
    template bool PermissionManager::canRead<TYPE>(const Requester&, const uint64_t entity_id, HttpError&);               \
    template bool PermissionManager::canUpdate<TYPE>(const Requester&, const uint64_t entity_id, HttpError&);             \
    template bool PermissionManager::canDelete<TYPE>(const Requester&, const uint64_t entity_id, HttpError&);

#define INSTANTIATE_PERMISSION_CLIENT(TYPE)                                                                                 \
    INSTANTIATE_PERMISSION_CRUD(TYPE)                                                                                       \
    template bool PermissionManager::canGetServices<TYPE>(const Requester& requester, const uint64_t id, HttpError& error); \
    template bool PermissionManager::canToggleActive<TYPE>(const Requester& requester, const uint64_t id, HttpError& error);

#define INSTANTIATE_PERMISSION_ENTITY(TYPE) \
    INSTANTIATE_PERMISSION_CRUD(TYPE)       \
    template bool PermissionManager::canManageStaff<TYPE>(const Requester&, const uint64_t entity_id, HttpError&);

// Client types
INSTANTIATE_PERMISSION_CLIENT(User)
INSTANTIATE_PERMISSION_CLIENT(Provider)

// Entity types with staff management
INSTANTIATE_PERMISSION_ENTITY(Clinics)
INSTANTIATE_PERMISSION_ENTITY(Pharmacies)
INSTANTIATE_PERMISSION_ENTITY(Laboratories)
INSTANTIATE_PERMISSION_ENTITY(RadiologyCenters)

// Regular entities with CRUD
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

//[ ] create a cache for permissions and make update or delete cause invalidation.
