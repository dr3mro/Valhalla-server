#pragma once

#include <jsoncons/basic_json.hpp>
#include <memory>

#include "controllers/databasecontroller/databasecontroller.hpp"
#include "gatekeeper/permissionmanager/permissions.hpp"
#include "gatekeeper/types.hpp"
#include "utils/global/http.hpp"
#include "utils/global/requester.hpp"
namespace api
{
    namespace v2
    {
        class PermissionManager
        {
           public:
            PermissionManager()          = default;
            virtual ~PermissionManager() = default;
            bool hasPermission(const Requester& requester, const jsoncons::json& permissions_j, const Permissions::PowerLevel& powerlevel, Http::Error& error);

            bool isOwnerOfService(const Requester& requester, const jsoncons::json& permissions_j, Http::Error& error);
            bool isAdminOfService(const Requester& requester, const jsoncons::json& permissions_j, Http::Error& error);
            bool isStaffOfService(const Requester& requester, const jsoncons::json& permissions_j, Http::Error& error);

            template <Client_t T>
            bool canCreate(const Requester& requester, const std::string& group, const std::optional<jsoncons::json>& data_j, Http::Error& error);

            template <Client_t T>
            bool canRead(const Requester& requester, const std::string& group, uint64_t client_id, Http::Error& error);

            template <Client_t T>
            bool canUpdate(const Requester& requester, const std::string& group, uint64_t entity_id, Http::Error& error);

            template <Client_t T>
            bool canDelete(const Requester& requester, const std::string& group, uint64_t client_id, Http::Error& error);

            template <Client_t T>
            bool canToggleActive(const Requester& requester, const std::string& group, uint64_t client_id, Http::Error& error);

            template <Client_t T>
            bool canGetServices(const Requester& requester, const std::string& group, uint64_t client_id, Http::Error& error);

            template <Service_t T>
            bool canCreate(const Requester& requester, const std::string& service_name, const std::optional<jsoncons::json>& service_j, Http::Error& error);

            template <Service_t T>
            bool canRead(const Requester& requester, const std::string& service_name, uint64_t service_id, Http::Error& error);

            template <Service_t T>
            bool canUpdate(const Requester& requester, const std::string& service_name, uint64_t service_id, Http::Error& error);

            template <Service_t T>
            bool canDelete(const Requester& requester, const std::string& service_name, uint64_t service_id, Http::Error& error);

            template <typename T>
            bool canManageStaff(const Requester& requester, const std::string& service_name, uint64_t service_id, Http::Error& error);

            template <Case_t T>
            bool canCreate(const Requester& requester, const std::string& group, const std::optional<jsoncons::json>& case_j, Http::Error& error);

            template <Case_t T>
            bool canRead(const Requester& requester, const std::string& group, uint64_t service_id, Http::Error& error);

            template <Case_t T>
            bool canUpdate(const Requester& requester, const std::string& group, uint64_t service_id, Http::Error& error);

            template <Case_t T>
            bool canDelete(const Requester& requester, const std::string& group, uint64_t service_id, Http::Error& error);

            template <Appointment_t T>
            bool canCreate(const Requester& requester, const std::string& group, const std::optional<jsoncons::json>& service_j, Http::Error& error);

            template <Appointment_t T>
            bool canRead(const Requester& requester, const std::string& group, uint64_t service_id, Http::Error& error);

            template <Appointment_t T>
            bool canUpdate(const Requester& requester, const std::string& group, uint64_t service_id, Http::Error& error);

            template <Appointment_t T>
            bool canDelete(const Requester& requester, const std::string& group, uint64_t id, Http::Error& error);

            //    private:
            //     bool isSameClient(const uint64_t clientID);
            //     bool hasPower(std::string_view data, const uint64_t clientID);
            //     bool isServiceOwner(std::string_view data, const uint64_t clientID);
            //     bool isServiceAdmin(std::string_view data, const uint64_t clientID);
            //     bool isServiceStaff(std::string_view data, const uint64_t clientID);
            //     bool isServicePatient(std::string_view data, const uint64_t clientID);
            std::shared_ptr<DatabaseController> db_ctl = Store::getObject<DatabaseController>();
        };
    }  // namespace v2
}  // namespace api
