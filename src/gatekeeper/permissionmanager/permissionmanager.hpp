#pragma once

#include <cstdint>
#include <jsoncons/basic_json.hpp>
#include <memory>

#include "controllers/databasecontroller/databasecontroller.hpp"
#include "gatekeeper/permissionmanager/permissionmanager_private.hpp"
#include "utils/global/concepts.hpp"
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
            // client is either user or provider
            template <Client_t T>
            bool canCreate(const Requester& requester, const std::optional<jsoncons::json>& data_j, Http::Error& error);

            template <Client_t T>
            bool canRead(const Requester& requester, uint64_t client_id, Http::Error& error);

            template <Client_t T>
            bool canUpdate(const Requester& requester, uint64_t client_id, Http::Error& error);

            template <Client_t T>
            bool canDelete(const Requester& requester, uint64_t client_id, Http::Error& error);

            template <Client_t T>
            bool canToggleActive(const Requester& requester, uint64_t client_id, Http::Error& error);

            template <Client_t T>
            bool canGetServices(const Requester& requester, uint64_t client_id, Http::Error& error);

            // Service is either a clinic, pharmacy, lab or rad center
            template <Service_t T>
            bool canCreate(const Requester& requester, const std::optional<jsoncons::json>& service_j, Http::Error& error);

            template <Service_t T>
            bool canRead(const Requester& requester, uint64_t service_id, Http::Error& error);

            template <Service_t T>
            bool canUpdate(const Requester& requester, uint64_t service_id, Http::Error& error);

            template <Service_t T>
            bool canDelete(const Requester& requester, uint64_t service_id, Http::Error& error);

            template <typename T>
            bool canManageStaff(const Requester& requester, uint64_t service_id, Http::Error& error);

            // case is a data set inside clinic workflow

            template <Case_t T>
            bool canCreate(const Requester& requester, const std::optional<jsoncons::json>& data_j, Http::Error& error);

            template <Case_t T>
            bool canRead(const Requester& requester, uint64_t case_id, Http::Error& error);

            template <Case_t T>
            bool canUpdate(const Requester& requester, uint64_t case_id, Http::Error& error);

            template <Case_t T>
            bool canDelete(const Requester& requester, uint64_t id, Http::Error& error);

            // appointment is either for any service
            template <Appointment_t T>
            bool canCreate(const Requester& requester, const std::optional<jsoncons::json>& service_j, Http::Error& error);

            template <Appointment_t T>
            bool canRead(const Requester& requester, uint64_t service_id, Http::Error& error);

            template <Appointment_t T>
            bool canUpdate(const Requester& requester, uint64_t service_id, Http::Error& error);

            template <Appointment_t T>
            bool canDelete(const Requester& requester, uint64_t id, Http::Error& error);

           private:
            std::shared_ptr<DatabaseController>       db_ctl  = Store::getObject<DatabaseController>();
            std::shared_ptr<PermissionManagerPrivate> pm_priv = Store::getObject<PermissionManagerPrivate>();
        };
    }  // namespace v2
}  // namespace api
