#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include "gatekeeper/keeprbase/keeprbase.hpp"
#include "gatekeeper/passwordcrypt/passwordcrypt.hpp"
#include "gatekeeper/types.hpp"
#include "store/store.hpp"
#include "utils/global/callback.hpp"
#include "utils/memcache/memcache.hpp"

namespace api
{
    namespace v2
    {

        class SessionManager : public KeeprBase
        {
           public:
            SessionManager()                                 = default;
            SessionManager(const SessionManager&)            = default;
            SessionManager(SessionManager&&)                 = delete;
            SessionManager& operator=(const SessionManager&) = delete;
            SessionManager& operator=(SessionManager&&)      = delete;
            ~SessionManager() override                       = default;

            bool login(const std::optional<Types::Credentials>& credentials, std::optional<Types::ClientLoginData>& clientLoginData, std::string& message);
            void logout(CALLBACK_&& callback, std::optional<Types::ClientLoginData>& clientLoginData);
            bool clientHasValidSession(std::optional<Types::ClientLoginData>& clientLoginData, std::string& message);
            bool storeSession(std::optional<Types::ClientLoginData>& clientLoginData, std::string& message);
            bool removeSession(std::optional<Types::ClientLoginData>& clientLoginData);

           private:
            std::shared_ptr<PasswordCrypt>                    passwordCrypt = Store::getObject<PasswordCrypt>();
            std::shared_ptr<MemCache<Types::ClientLoginData>> clientsSessionsList =
                Store::getObject<MemCache<Types::ClientLoginData>>(4096, std::chrono::seconds(3600));
        };
    }  // namespace v2
}  // namespace api