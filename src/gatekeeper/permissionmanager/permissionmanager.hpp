#pragma once
#include <functional>
#include <optional>
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
            bool hasPermission(CALLBACK_&& callback, std::string_view data);
        };
    }  // namespace v2
}  // namespace api