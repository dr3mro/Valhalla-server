#include "gatekeeper/permissionmanager/permissionmanager_private.hpp"

#include <jsoncons/json.hpp>
#include <string>

#include "gatekeeper/permissionmanager/permissions.hpp"
#include "utils/global/http.hpp"

using PowerLevel = Permissions::PowerLevel;

using namespace api::v2;
bool PermissionManagerPrivate::hasPermission(const Requester& requester, const std::optional<jsoncons::json>& permissions_j,
    const Permissions::PowerLevel& powerlevel, const std::string& service_name, Http::Error& error)
{
    auto entityStaffPermissions = this->isStaffOfService(requester, permissions_j, service_name, error);

    if (!entityStaffPermissions.has_value())
    {
        error.code    = Http::Status::FORBIDDEN;
        error.message = "You are not a staff of this service";
        return false;
    }

    return (bool(entityStaffPermissions->power & powerlevel) != 0);
}
bool PermissionManagerPrivate::isOwnerOfService(
    const Requester& requester, const std::optional<jsoncons::json>& permissions_j, const std::string& service_name, Http::Error& error)
{
    if (!permissions_j.has_value() || permissions_j->empty())
    {
        error.message = "Failed get service permissions for." + service_name;
        error.code    = api::v2::Http::BAD_REQUEST;
        return false;
    }

    if (requester.id != permissions_j.value()["owner_id"].as<uint64_t>())
    {
        error.code = Http::Status::FORBIDDEN;
        error.message += "You are not the owner of " + service_name;
        return false;
    }
    return true;
}

bool PermissionManagerPrivate::isAdminOfService(
    const Requester& requester, const std::optional<jsoncons::json>& permissions_j, const std::string& service_name, Http::Error& error)
{
    if (!permissions_j.has_value() || permissions_j->empty())
    {
        error.message = "Failed get service permissions for." + service_name;
        error.code    = api::v2::Http::BAD_REQUEST;
        return false;
    }

    if (requester.id != permissions_j.value()["admin_id"].as<uint64_t>())
    {
        error.code = Http::Status::FORBIDDEN;
        error.message += "You are not the admin of " + service_name;
        return false;
    }
    return true;
}
std::optional<Permissions::StaffPermission> PermissionManagerPrivate::isStaffOfService(
    const Requester& requester, const std::optional<jsoncons::json>& permissions_j, const std::string& service_name, Http::Error& error)
{
    try
    {
        if (!permissions_j.has_value() || permissions_j->empty())
        {
            error.message = "Failed get service permissions for." + service_name;
            error.code    = api::v2::Http::BAD_REQUEST;
            return std::nullopt;
        }

        const auto& staff = permissions_j.value()["staff"];

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
                            .staff_id = static_cast<uint64_t>(std::stoull(left)), .power = static_cast<PowerLevel>(std::stoi(right)), .isMember = true});
                    }
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        error.code    = Http::Status::FORBIDDEN;
        error.message = std::string("Invalid staff permission: ") + e.what();
    }

    error.code    = Http::Status::FORBIDDEN;
    error.message = "Failed to find the user permission level in service " + service_name;
    return std::nullopt;
}

bool PermissionManagerPrivate::assert_group_id_match(const Requester& requester, const std::string& groupname, uint64_t client_id, Http::Error& error)
{
    if (requester.group != groupname || requester.id != client_id)
    {
        error.code    = Http::Status::FORBIDDEN;
        error.message = "You are not allowed to manage client with id: " + std::to_string(client_id) + " and group: " + groupname;
        return false;
    }

    return true;
}
bool api::v2::PermissionManagerPrivate::preServiceCreateChecks(const Requester& requester, const std::optional<jsoncons::json>& service_j, Http::Error& error)
{
    if (!service_j.has_value())
    {
        error.code    = Http::Status::BAD_REQUEST;
        error.message = "Service data is not provided.";
        return false;
    }

    uint64_t owner_id;
    uint64_t admin_id;

    try
    {
        owner_id = service_j->at("owner_id").as<uint64_t>();
        admin_id = service_j->at("admin_id").as<uint64_t>();
    }
    catch (const std::exception& e)
    {
        error.code    = Http::Status::BAD_REQUEST;
        error.message = std::string("failed to extract id ") + e.what();
        return false;
    }

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

bool api::v2::PermissionManagerPrivate::isOwnerOrAdminOrHasPermission(
    const Requester& requester, const std::optional<jsoncons::json>& permissions_j, const std::string& service_name, Http::Error& error)
{
    if (this->isOwnerOrAdmin(requester, permissions_j, service_name, error))
    {
        return true;
    }

    if (!this->hasPermission(requester, permissions_j, Permissions::PowerLevel::CAN_DELETE, service_name, error))
    {
        error.code    = Http::Status::FORBIDDEN;
        error.message = fmt::format("You don't have the permission to manage  {} {}", service_name, error.message);
        return false;
    }
    return true;
}

bool api::v2::PermissionManagerPrivate::isOwnerOrAdmin(
    const Requester& requester, const std::optional<jsoncons::json>& permissions_j, const std::string& service_name, Http::Error& error)
{
    return (this->isOwnerOfService(requester, permissions_j, service_name, error) || this->isAdminOfService(requester, permissions_j, service_name, error));
}
