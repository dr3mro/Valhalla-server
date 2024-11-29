#pragma once
#include <functional>

#include "utils/global/callback.hpp"

namespace api
{
    namespace v2
    {
        class SessionManager
        {
           public:
            SessionManager()          = default;
            virtual ~SessionManager() = default;

            void login(CALLBACK_&& callback, std::string_view data);
            void logout(CALLBACK_&& callback, std::string_view data);
            void isSessionValid(CALLBACK_&& callback, std::string_view data);
        };
    }  // namespace v2
}  // namespace api