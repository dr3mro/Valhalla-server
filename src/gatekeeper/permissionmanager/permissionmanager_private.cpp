#include "gatekeeper/permissionmanager/permissionmanager_private.hpp"

#include <jsoncons/json.hpp>
#include <string>

#include "gatekeeper/permissionmanager/permissions.hpp"
#include "utils/global/http.hpp"

using PowerLevel = Permissions::PowerLevel;

using namespace api::v2;
bool PermissionManagerPrivate::hasPermission(
    const std::optional<Permissions::StaffPermission>& entityStaffPermissions, const Permissions::PowerLevel& powerlevel)
{
    return (bool(entityStaffPermissions->power & powerlevel) != 0);
}
bool PermissionManagerPrivate::isOwnerOfService(const Requester& requester, const jsoncons::json& permissions_j, Http::Error& error)
{
    if (requester.id != permissions_j["owner_id"].as<uint64_t>())
    {
        error.code = Http::Status::FORBIDDEN;
        error.message += "You are not the owner of this service ";
        return false;
    }
    return true;
}

bool PermissionManagerPrivate::isAdminOfService(const Requester& requester, const jsoncons::json& permissions_j, Http::Error& error)
{
    if (requester.id != permissions_j["admin_id"].as<uint64_t>())
    {
        error.code = Http::Status::FORBIDDEN;
        error.message += "You are not the admin of this service ";
        return false;
    }
    return true;
}
std::optional<Permissions::StaffPermission> PermissionManagerPrivate::isStaffOfService(
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