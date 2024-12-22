#pragma once

#include <cstdint>
#include <exception>
#include <jsoncons/basic_json.hpp>
#include <memory>
#include <optional>
#include <string>

#include "controllers/databasecontroller/databasecontroller.hpp"
#include "gatekeeper/permissionmanager/permissions.hpp"
#include "store/store.hpp"
#include "utils/global/http.hpp"
#include "utils/global/requester.hpp"
namespace api::v2
{
    class PermissionManagerPrivate
    {
       public:
        PermissionManagerPrivate()                                                     = default;
        virtual ~PermissionManagerPrivate()                                            = default;
        PermissionManagerPrivate(const PermissionManagerPrivate& other)                = default;
        PermissionManagerPrivate& operator=(const PermissionManagerPrivate& other)     = default;
        PermissionManagerPrivate(PermissionManagerPrivate&& other) noexcept            = default;
        PermissionManagerPrivate& operator=(PermissionManagerPrivate&& other) noexcept = default;

        static bool hasPermission(const Requester& requester, const std::optional<jsoncons::json>& permissions_j, const Permissions::PowerLevel& powerlevel,
            const std::string& service_name, Http::Error& error);

        static bool isOwnerOfService(
            const Requester& requester, const std::optional<jsoncons::json>& permissions_j, const std::string& service_name, Http::Error& error);

        static bool isAdminOfService(
            const Requester& requester, const std::optional<jsoncons::json>& permissions_j, const std::string& service_name, Http::Error& error);

        static std::optional<Permissions::StaffPermission> isStaffOfService(
            const Requester& requester, const std::optional<jsoncons::json>& permissions_j, const std::string& service_name, Http::Error& error);

        static bool assert_group_id_match(const Requester& requester, const std::string& groupname, uint64_t client_id, Http::Error& error);

        static bool preServiceCreateChecks(const Requester& requester, const std::optional<jsoncons::json>& service_j, Http::Error& error);

        static bool isOwnerOrAdminOrHasPermission(
            const Requester& requester, const std::optional<jsoncons::json>& permissions_j, const std::string& service_name, Http::Error& error);

        static bool isOwnerOrAdmin(
            const Requester& requester, const std::optional<jsoncons::json>& permissions_j, const std::string& service_name, Http::Error& error);

        template <typename T>
        static std::optional<T> extract_json_value_safely(
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
        static std::optional<jsoncons::json> getPermissionsOfEntity(Func&& func, Args&... args)
        {
            static std::shared_ptr<DatabaseController> db_ctl = Store::getObject<DatabaseController>();
            std::optional<std::string>                 query  = std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);

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
    };

}  // namespace api::v2
