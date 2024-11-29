#pragma once
#include <functional>

#include "gatekeeper/keeprbase/keeprbase.hpp"
#include "utils/global/callback.hpp"
#include "utils/memcache/memcache.hpp"

namespace api
{
    namespace v2
    {

        class SessionManager : public KeeprBase
        {
           public:
            SessionManager()          = default;
            virtual ~SessionManager() = default;

            bool login(CALLBACK_&& callback, const std::optional<Types::Credentials>& credentials,
                       std::optional<Types::ClientLoginData>& clientLoginData);
            void logout(CALLBACK_&& callback, std::optional<Types::ClientLoginData>& clientLoginData);
            bool clientHasValidSession(std::optional<Types::ClientLoginData>& clientLoginData, std::string& message);
            bool storeSession(std::optional<Types::ClientLoginData>& clientLoginData, std::string& message);
            bool removeSession(std::optional<Types::ClientLoginData>& clientLoginData);

           private:
            std::shared_ptr<PasswordCrypt>                    passwordCrypt       = Store::getObject<PasswordCrypt>();
            std::shared_ptr<MemCache<Types::ClientLoginData>> clientsSessionsList = Store::getObject<MemCache<Types::ClientLoginData>>();
        };
    }  // namespace v2
}  // namespace api