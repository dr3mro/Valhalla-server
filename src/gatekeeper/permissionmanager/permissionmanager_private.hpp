#pragma once

#include <optional>

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
            bool hasPermission(const Requester& requester, const std::optional<jsoncons::json>& permissions_j, const Permissions::PowerLevel& powerlevel,
                const std::string& service_name, Http::Error& error);

            bool isOwnerOfService(
                const Requester& requester, const std::optional<jsoncons::json>& permissions_j, const std::string& service_name, Http::Error& error);

            bool isAdminOfService(
                const Requester& requester, const std::optional<jsoncons::json>& permissions_j, const std::string& service_name, Http::Error& error);

            std::optional<Permissions::StaffPermission> isStaffOfService(
                const Requester& requester, const std::optional<jsoncons::json>& permissions_j, const std::string& service_name, Http::Error& error);

            bool assert_group_id_match(const Requester& requester, const std::string& groupname, uint64_t client_id, Http::Error& error);

            bool preServiceCreateChecks(const Requester& requester, const std::optional<jsoncons::json>& service_j, Http::Error& error);

            bool isOwnerOrAdminOrHasPermission(
                const Requester& requester, const std::optional<jsoncons::json>& permissions_j, const std::string& service_name, Http::Error& error);

            bool isOwnerOrAdmin(
                const Requester& requester, const std::optional<jsoncons::json>& permissions_j, const std::string& service_name, Http::Error& error);

            template <typename T>
            std::optional<T> extract_json_value_safely(
                const std::optional<jsoncons::json>& service_j, const std::string& key, const std::string& service_name, Http::Error& error)
            {
                if (!service_j.has_value() || service_j->empty())
                {
                    error.code    = Http::Status::BAD_REQUEST;
                    error.message = "Service data is not provided for " + service_name;
                    return std::nullopt;
                }

                T value;
                try
                {
                    value = service_j->at(key).as<T>();
                }
                catch (const std::exception& e)
                {
                    error.message = "Failed to extract " + key;
                    error.code    = Http::Status::BAD_REQUEST;
                    return std::nullopt;
                }
                return value;
            }

            template <typename Func, typename... Args>
            std::optional<jsoncons::json> getPermissionsOfEntity(Func&& func, Args&... args)
            {
                std::optional<std::string> query = std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);

                if (!query.has_value() || query->empty())
                {
                    return std::nullopt;
                }

                std::optional<jsoncons::json> permissions_j = db_ctl->getPermissions(query.value());

                if (!permissions_j.has_value() || permissions_j->empty())
                {
                    return std::nullopt;
                }

                return permissions_j;
            }

           private:
            std::shared_ptr<DatabaseController> db_ctl = Store::getObject<DatabaseController>();
        };
    }  // namespace v2
}  // namespace api
