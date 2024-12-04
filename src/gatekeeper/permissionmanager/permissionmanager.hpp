#pragma once

#include "gatekeeper/permissionmanager/context.hpp"
namespace api
{
    namespace v2
    {
        class PermissionManager
        {
           public:
            PermissionManager()          = default;
            virtual ~PermissionManager() = default;
            bool hasPermission(const Context& context);

            //    private:
            //     bool isSameClient(const uint64_t clientID);
            //     bool hasPower(std::string_view data, const uint64_t clientID);
            //     bool isServiceOwner(std::string_view data, const uint64_t clientID);
            //     bool isServiceAdmin(std::string_view data, const uint64_t clientID);
            //     bool isServiceStaff(std::string_view data, const uint64_t clientID);
            //     bool isServicePatient(std::string_view data, const uint64_t clientID);
        };
    }  // namespace v2
}  // namespace api
