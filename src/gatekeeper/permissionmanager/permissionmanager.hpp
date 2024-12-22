#pragma once

#include <cstdint>
#include <jsoncons/basic_json.hpp>
#include <optional>

#include "utils/global/concepts.hpp"
#include "utils/global/http.hpp"
#include "utils/global/requester.hpp"
namespace api::v2
{
    class PermissionManager
    {
       public:
        PermissionManager()                                    = default;
        PermissionManager(const PermissionManager&)            = default;
        PermissionManager& operator=(const PermissionManager&) = default;
        PermissionManager(PermissionManager&&)                 = default;
        PermissionManager& operator=(PermissionManager&&)      = default;
        virtual ~PermissionManager()                           = default;

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
        bool canDelete(const Requester& requester, uint64_t _id, Http::Error& error);

        // appointment is either for any service
        template <Appointment_t T>
        bool canCreate(const Requester& requester, const std::optional<jsoncons::json>& service_j, Http::Error& error);

        template <Appointment_t T>
        bool canRead(const Requester& requester, uint64_t service_id, Http::Error& error);

        template <Appointment_t T>
        bool canUpdate(const Requester& requester, uint64_t service_id, Http::Error& error);

        template <Appointment_t T>
        bool canDelete(const Requester& requester, uint64_t _id, Http::Error& error);

       private:
    };
}  // namespace api::v2
