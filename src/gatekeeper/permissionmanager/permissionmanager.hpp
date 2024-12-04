#pragma once
#include <string_view>

#include "utils/global/callback.hpp"

namespace api
{
    namespace v2
    {
        class PermissionManager
        {
           public:
            PermissionManager()          = default;
            virtual ~PermissionManager() = default;
            // bool hasPermission(CALLBACK_&& callback, std::string_view data); //TODO:
        };
    }  // namespace v2
}  // namespace api