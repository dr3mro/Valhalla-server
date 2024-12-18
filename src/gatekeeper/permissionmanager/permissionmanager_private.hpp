#pragma once

#include "controllers/databasecontroller/databasecontroller.hpp"
#include "gatekeeper/permissionmanager/permissions.hpp"
#include "utils/global/http.hpp"
#include "utils/global/requester.hpp"
namespace api
{
    namespace v2
    {
        class PermissionManagerPrivate
        {
           public:
            PermissionManagerPrivate()          = default;
            virtual ~PermissionManagerPrivate() = default;
            bool hasPermission(const std::optional<Permissions::StaffPermission>& entityStaffPermissions, const Permissions::PowerLevel& powerlevel);

            bool                                        isOwnerOfService(const Requester& requester, const jsoncons::json& permissions_j, Http::Error& error);
            bool                                        isAdminOfService(const Requester& requester, const jsoncons::json& permissions_j, Http::Error& error);
            std::optional<Permissions::StaffPermission> isStaffOfService(const Requester& requester, const jsoncons::json& permissions_j, Http::Error& error);

            bool assert_group_id_match(const Requester& requester, const std::string& groupname, uint64_t client_id, Http::Error& error);

           private:
            std::shared_ptr<DatabaseController> db_ctl = Store::getObject<DatabaseController>();
        };
    }  // namespace v2
}  // namespace api
