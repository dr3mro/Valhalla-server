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

namespace api::v2
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
        static constexpr std::chrono::seconds SESSION_TIMEOUT = std::chrono::seconds(3600);
        static constexpr int                  SESSION_MAX     = 4096;

        std::shared_ptr<PasswordCrypt>                    passwordCrypt = Store::getObject<PasswordCrypt>();
        std::shared_ptr<MemCache<Types::ClientLoginData>> clientsSessionsList =
            Store::getObject<MemCache<Types::ClientLoginData>>(SESSION_MAX, SESSION_TIMEOUT);
    };
}  // namespace api::v2
