#pragma once
#include <functional>
#include <string_view>

#include "utils/global/callback.hpp"

namespace api
{
    namespace v2
    {
        class GateKeeper
        {
           public:
            GateKeeper()          = default;
            virtual ~GateKeeper() = default;

            void login(CALLBACK_&& callback, std::string_view data);
            void logout(CALLBACK_&& callback, std::string_view data);

            bool isAuthenticated(CALLBACK_&& callback, std::string_view data);
            bool hasPermission(CALLBACK_&& callback, std::string_view data);

            bool isDosAttack(CALLBACK_&& callback, std::string_view data);
        };
    }  // namespace v2
}  // namespace api
