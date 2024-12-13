#include "gatekeeper/permissionmanager/permissionmanager.hpp"

#include <cstdint>
#include <exception>
#include <jsoncons/basic_json.hpp>
#include <memory>
#include <optional>

#include "gatekeeper/permissionmanager/permissions.hpp"
#include "utils/global/http.hpp"

using PowerLevel = Permissions::PowerLevel;

using namespace api::v2;
bool PermissionManager::hasPermission(const std::optional<Permissions::StaffPermission>& entityStaffPermissions, const Permissions::PowerLevel& powerlevel)
{
    return (bool(entityStaffPermissions->power & powerlevel) != 0);
}
bool PermissionManager::isOwnerOfService(const Requester& requester, const jsoncons::json& permissions_j, Http::Error& error)
{
    if (requester.id != permissions_j["owner_id"].as<uint64_t>())
    {
        error.code = Http::Status::FORBIDDEN;
        error.message += "You are not the owner of this service ";
        return false;
    }
    return true;
}

bool PermissionManager::isAdminOfService(const Requester& requester, const jsoncons::json& permissions_j, Http::Error& error)
{
    if (requester.id != permissions_j["admin_id"].as<uint64_t>())
    {
        error.code = Http::Status::FORBIDDEN;
        error.message += "You are not the admin of this service ";
        return false;
    }
    return true;
}
std::optional<Permissions::StaffPermission> PermissionManager::isStaffOfService(
    const Requester& requester, const jsoncons::json& permissions_j, Http::Error& error)
{
    try
    {
        const auto& staff = permissions_j["staff"];

        for (const auto& [role, members] : staff.object_range())
        {
            for (const auto& member : members.array_range())
            {
                if (!member.as<std::string>().empty())
                {
                    auto        raw_str = member.as<std::string>();
                    size_t      pos     = raw_str.find(':');
                    std::string left    = raw_str.substr(0, pos);
                    std::string right   = raw_str.substr(pos + 1);
                    if (left == std::to_string(requester.id))
                    {
                        return std::optional<Permissions::StaffPermission>(Permissions::StaffPermission{
                            .id = static_cast<uint64_t>(std::stoull(left)), .power = static_cast<PowerLevel>(std::stoi(right)), .isMember = true});
                    }
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        error.code    = Http::Status::BAD_REQUEST;
        error.message = std::string("Invalid staff permission: ") + e.what();
    }
    return std::nullopt;
}
template <Client_t T>
bool PermissionManager::canCreate([[maybe_unused]] const Requester& requester, [[maybe_unused]] const std::string& group,
    [[maybe_unused]] const std::optional<jsoncons::json>& data_j, [[maybe_unused]] Http::Error& error)
{
    return true;
}

template <Client_t T>
bool PermissionManager::canRead(const Requester& requester, const std::string& group, uint64_t client_id, Http::Error& error)
{
    if (requester.group != group || requester.id != client_id)
    {
        error.code    = Http::Status::FORBIDDEN;
        error.message = "You are not allowed to read this client";
        return false;
    }

    return true;
}
template <Client_t T>
bool PermissionManager::canUpdate(const Requester& requester, const std::string& group, uint64_t id, Http::Error& error)
{
    if (requester.id != id || requester.group != group)
    {
        error.code    = Http::Status::FORBIDDEN;
        error.message = "You are not allowed to update this client";
        return false;
    }
    return true;
}
template <Client_t T>
bool PermissionManager::canDelete(const Requester& requester, const std::string& group, const uint64_t client_id, Http::Error& error)
{
    if (requester.group != group || requester.id != client_id)
    {
        error.code    = Http::Status::FORBIDDEN;
        error.message = "You are not allowed to delete this client";
        return false;
    }

    return true;
}

template <Client_t T>
bool PermissionManager::canToggleActive(const Requester& requester, const std::string& group, const uint64_t client_id, Http::Error& error)
{
    if (requester.group != group || requester.id != client_id)
    {
        error.code    = Http::Status::FORBIDDEN;
        error.message = "You are not allowed to change active state of this client";
        return false;
    }

    return true;
}

template <Client_t T>
bool PermissionManager::canGetServices(const Requester& requester, const std::string& group, const uint64_t client_id, Http::Error& error)
{
    if (requester.group != group || requester.id != client_id)
    {
        error.code    = Http::Status::FORBIDDEN;
        error.message = "You are not allowed to get the services list of this client";
        return false;
    }

    return true;
}

template <Service_t T>
bool PermissionManager::canCreate(
    const Requester& requester, [[maybe_unused]] const std::string& service_name, const std::optional<jsoncons::json>& service_j, Http::Error& error)
{
    if (!service_j.has_value())
    {
        error.code    = Http::Status::BAD_REQUEST;
        error.message = "Service is not provided.";
        return false;
    }

    uint64_t owner_id = service_j->at("owner_id").as<uint64_t>();
    uint64_t admin_id = service_j->at("admin_id").as<uint64_t>();

    if (owner_id != requester.id || admin_id != requester.id)
    {
        error.code    = Http::Status::BAD_REQUEST;
        error.message = "Initial service admin_id and owner_id should be equal to that of provider_id";
        return false;
    }

    if (requester.group != "providers")
    {
        error.code    = Http::Status::BAD_REQUEST;
        error.message = "You are not allowed to create a service, you are not a provider.";
        return false;
    }
    return true;
}

template <Service_t T>
bool PermissionManager::canRead(const Requester& requester, const std::string& service_name, uint64_t service_id, Http::Error& error)
{
    const jsoncons::json permissions_j = db_ctl->getServicePermissions(service_name, service_id);
    if (isOwnerOfService(requester, permissions_j, error))
    {
        return true;
    }
    if (isAdminOfService(requester, permissions_j, error))
    {
        return true;
    }
    auto serviceStaff = isStaffOfService(requester, permissions_j, error);
    if (!serviceStaff.has_value())
    {
        error.code    = Http::Status::BAD_REQUEST;
        error.message = "Error reading service staff " + service_name;
        return false;
    }
    if (!hasPermission(serviceStaff, Permissions::PowerLevel::CAN_READ))
    {
        error.code    = Http::Status::FORBIDDEN;
        error.message = "You don't have the permission to read this service";
        return false;
    }
    return true;
}
template <Service_t T>
bool PermissionManager::canUpdate(const Requester& requester, const std::string& service_name, uint64_t service_id, Http::Error& error)
{
    const jsoncons::json permissions_j = db_ctl->getServicePermissions(service_name, service_id);
    if (isOwnerOfService(requester, permissions_j, error))
    {
        return true;
    }
    if (isAdminOfService(requester, permissions_j, error))
    {
        return true;
    }

    auto serviceStaff = isStaffOfService(requester, permissions_j, error);
    if (!serviceStaff.has_value())
    {
        error.code    = Http::Status::BAD_REQUEST;
        error.message = "You are not a staff of this service";
        return false;
    }

    if (!hasPermission(serviceStaff, Permissions::PowerLevel::CAN_WRITE))
    {
        error.code    = Http::Status::FORBIDDEN;
        error.message = "You don't have the permission to update this service";
        return false;
    }
    return true;
}

template <Service_t T>
bool PermissionManager::canDelete(const Requester& requester, const std::string& service_name, const uint64_t service_id, Http::Error& error)
{
    const jsoncons::json permissions_j = db_ctl->getServicePermissions(service_name, service_id);
    if (isOwnerOfService(requester, permissions_j, error))
    {
        return true;
    }
    return false;
}
template <typename T>
bool PermissionManager::canManageStaff(const Requester& requester, const std::string& service_name, uint64_t service_id, Http::Error& error)
{
    const jsoncons::json permissions_j = db_ctl->getServicePermissions(service_name, service_id);
    if (isOwnerOfService(requester, permissions_j, error) || isAdminOfService(requester, permissions_j, error))
    {
        return true;
    }
    return false;
}

template <Case_t T>
bool PermissionManager::canCreate(const Requester& requester, const std::string& group, const std::optional<jsoncons::json>& case_j, Http::Error& error)
{
    if (!case_j.has_value())
    {
        error.code    = Http::Status::BAD_REQUEST;
        error.message = "proper data is not provided: " + group;
        return false;
    }

    // TODO: change the clinic_id to service_id in all sql schema.

    uint64_t clinic_id = case_j->at("clinic_id").as<uint64_t>();

    std::optional<jsoncons::json> permissions_j = db_ctl->getServicePermissions(T::getOrgName(), clinic_id);
    if (!permissions_j.has_value())
    {
        error.code    = Http::Status::BAD_REQUEST;
        error.message = "clinic_id is not provided. " + group;
        return false;
    }
    if (isOwnerOfService(requester, permissions_j.value(), error))
    {
        return true;
    }
    if (isAdminOfService(requester, permissions_j.value(), error))
    {
        return true;
    }
    auto serviceStaff = isStaffOfService(requester, permissions_j, error);
    if (!serviceStaff.has_value())
    {
        error.code    = Http::Status::FORBIDDEN;
        error.message = "You are not a staff of this service";
        return false;
    }

    if (!hasPermission(serviceStaff, Permissions::PowerLevel::CAN_WRITE))
    {
        error.code    = Http::Status::FORBIDDEN;
        error.message = "You don't have the permission to create " + std::string(T::getTableName());
        return false;
    }
    return true;
}

template <Case_t T>
bool PermissionManager::canRead(const Requester& requester, const std::string& group, uint64_t service_id, Http::Error& error)
{
    std::optional<jsoncons::json> permissions_j = db_ctl->getServicePermissions(T::getOrgName(), service_id);

    if (isOwnerOfService(requester, permissions_j.value(), error))
    {
        return true;
    }
    if (isAdminOfService(requester, permissions_j.value(), error))
    {
        return true;
    }
    auto serviceStaff = isStaffOfService(requester, permissions_j, error);
    if (!serviceStaff.has_value())
    {
        error.code    = Http::Status::BAD_REQUEST;
        error.message = fmt::format("{}:{}", "Error reading service staff ", group);
        return false;
    }

    if (!hasPermission(serviceStaff, Permissions::PowerLevel::CAN_READ))
    {
        error.code    = Http::Status::FORBIDDEN;
        error.message = "You don't have the permission to read " + std::string(T::getTableName());
        return false;
    }
    return true;
}

template <Case_t T>
bool PermissionManager::canUpdate(const Requester& requester, const std::string& group, const uint64_t service_id, Http::Error& error)
{
    std::optional<jsoncons::json> permissions_j = db_ctl->getServicePermissions(T::getOrgName(), service_id);

    if (isOwnerOfService(requester, permissions_j.value(), error))
    {
        return true;
    }
    if (isAdminOfService(requester, permissions_j.value(), error))
    {
        return true;
    }
    auto serviceStaff = isStaffOfService(requester, permissions_j, error);

    if (!serviceStaff.has_value())
    {
        error.code    = Http::Status::BAD_REQUEST;
        error.message = fmt::format("{}:{}", "Error reading service staff ", group);
        return false;
    }

    if (!hasPermission(serviceStaff, Permissions::PowerLevel::CAN_WRITE))
    {
        error.code    = Http::Status::FORBIDDEN;
        error.message = "You don't have the permission to update " + std::string(T::getTableName());
        return false;
    }
    return true;
}

template <Case_t T>
bool PermissionManager::canDelete(const Requester& requester, const std::string& group, uint64_t service_id, Http::Error& error)
{
    std::optional<jsoncons::json> permissions_j = db_ctl->getServicePermissions(T::getOrgName(), service_id);

    if (isOwnerOfService(requester, permissions_j.value(), error))
    {
        return true;
    }
    if (isAdminOfService(requester, permissions_j.value(), error))
    {
        return true;
    }
    auto serviceStaff = isStaffOfService(requester, permissions_j, error);
    if (!serviceStaff.has_value())
    {
        error.code    = Http::Status::BAD_REQUEST;
        error.message = fmt::format("{}:{}", "Error reading service staff ", group);
        return false;
    }

    if (!hasPermission(serviceStaff, Permissions::PowerLevel::CAN_DELETE))
    {
        error.code    = Http::Status::FORBIDDEN;
        error.message = "You don't have the permission to delete " + std::string(T::getTableName());
        return false;
    }
    return true;
}

template <Appointment_t T>
bool PermissionManager::canCreate(const Requester& requester, const std::string& group, const std::optional<jsoncons::json>& service_j, Http::Error& error)
{
    if (!service_j.has_value())
    {
        error.code    = Http::Status::BAD_REQUEST;
        error.message = "Service is not provided.";
        return false;
    }

    // TODO: change the clinic_id to service_id in all sql schema.

    uint64_t clinic_id = service_j->at("clinic_id").as<uint64_t>();

    std::optional<jsoncons::json> permissions_j = db_ctl->getServicePermissions(group, clinic_id);
    if (!permissions_j.has_value())
    {
        error.code    = Http::Status::BAD_REQUEST;
        error.message = "clinic_id is not provided.";
        return false;
    }
    if (isOwnerOfService(requester, permissions_j.value(), error))
    {
        return true;
    }
    if (isAdminOfService(requester, permissions_j.value(), error))
    {
        return true;
    }
    auto serviceStaff = isStaffOfService(requester, permissions_j, error);
    if (!serviceStaff.has_value())
    {
        error.code    = Http::Status::BAD_REQUEST;
        error.message = "Error reading service staff " + group;
        return false;
    }

    if (!hasPermission(serviceStaff, Permissions::PowerLevel::CAN_WRITE))
    {
        error.code    = Http::Status::FORBIDDEN;
        error.message = "You don't have the permission to create " + std::string(T::getTableName());
        return false;
    }
    return true;
}

template <Appointment_t T>
bool PermissionManager::canRead(const Requester& requester, const std::string& group, uint64_t service_id, Http::Error& error)
{
    std::optional<jsoncons::json> permissions_j = db_ctl->getServicePermissions(group, service_id);

    if (isOwnerOfService(requester, permissions_j.value(), error))
    {
        return true;
    }
    if (isAdminOfService(requester, permissions_j.value(), error))
    {
        return true;
    }
    auto serviceStaff = isStaffOfService(requester, permissions_j, error);
    if (!serviceStaff.has_value())
    {
        error.code    = Http::Status::BAD_REQUEST;
        error.message = "Error reading service staff" + group;
        return false;
    }

    if (!hasPermission(serviceStaff, Permissions::PowerLevel::CAN_READ))
    {
        error.code    = Http::Status::FORBIDDEN;
        error.message = "You don't have the permission to read " + std::string(T::getTableName());
        return false;
    }
    return true;
}

template <Appointment_t T>
bool PermissionManager::canUpdate(const Requester& requester, const std::string& group, const uint64_t service_id, Http::Error& error)
{
    std::optional<jsoncons::json> permissions_j = db_ctl->getServicePermissions(group, service_id);

    if (isOwnerOfService(requester, permissions_j.value(), error))
    {
        return true;
    }
    if (isAdminOfService(requester, permissions_j.value(), error))
    {
        return true;
    }
    auto serviceStaff = isStaffOfService(requester, permissions_j, error);
    if (!serviceStaff.has_value())
    {
        error.code    = Http::Status::BAD_REQUEST;
        error.message = "Error reading service staff" + group;
        return false;
    }

    if (!hasPermission(serviceStaff, Permissions::PowerLevel::CAN_WRITE))
    {
        error.code    = Http::Status::FORBIDDEN;
        error.message = "You don't have the permission to update " + std::string(T::getTableName());
        return false;
    }
    return true;
}

template <Appointment_t T>
bool PermissionManager::canDelete(const Requester& requester, const std::string& group, uint64_t service_id, Http::Error& error)
{
    std::optional<jsoncons::json> permissions_j = db_ctl->getServicePermissions(group, service_id);

    if (isOwnerOfService(requester, permissions_j.value(), error))
    {
        return true;
    }
    if (isAdminOfService(requester, permissions_j.value(), error))
    {
        return true;
    }
    auto serviceStaff = isStaffOfService(requester, permissions_j, error);
    if (!serviceStaff.has_value())
    {
        error.code    = Http::Status::BAD_REQUEST;
        error.message = "Error reading service staff" + group;
        return false;
    }

    if (!hasPermission(serviceStaff, Permissions::PowerLevel::CAN_DELETE))
    {
        error.code    = Http::Status::FORBIDDEN;
        error.message = "You don't have the permission to delete " + std::string(T::getTableName());
        return false;
    }
    return true;
}

#include "gatekeeper/includes.hpp"  // IWYU pragma: keep

#define INSTANTIATE_PERMISSION_CRUD(TYPE)                                                                                                       \
    template bool PermissionManager::canCreate<TYPE>(const Requester&, const std::string&, const std::optional<jsoncons::json>&, Http::Error&); \
    template bool PermissionManager::canRead<TYPE>(const Requester&, const std::string&, const uint64_t entity_id, Http::Error&);               \
    template bool PermissionManager::canUpdate<TYPE>(const Requester&, const std::string&, const uint64_t entity_id, Http::Error&);             \
    template bool PermissionManager::canDelete<TYPE>(const Requester&, const std::string&, const uint64_t entity_id, Http::Error&);

#define INSTANTIATE_PERMISSION_CLIENT(TYPE)                                                                                                             \
    INSTANTIATE_PERMISSION_CRUD(TYPE)                                                                                                                   \
    template bool PermissionManager::canGetServices<TYPE>(const Requester& requester, const std::string& group, const uint64_t id, Http::Error& error); \
    template bool PermissionManager::canToggleActive<TYPE>(const Requester& requester, const std::string& group, const uint64_t id, Http::Error& error);

#define INSTANTIATE_PERMISSION_ENTITY(TYPE) \
    INSTANTIATE_PERMISSION_CRUD(TYPE)       \
    template bool PermissionManager::canManageStaff<TYPE>(const Requester&, const std::string&, const uint64_t entity_id, Http::Error&);

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
