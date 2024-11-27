#pragma once

#include <memory>
#include <string>

#include "controllers/databasecontroller/databasecontroller.hpp"
#include "store/store.hpp"

class SessionManager
{
   public:
    using LoggedClientInfo = struct LoggedClientInfo
    {
        std::optional<std::string> token;
        std::optional<std::string> userName;
        std::optional<std::string> group;
        std::optional<uint64_t>    clientId;
        std::optional<std::string> llodt;  // used to invalidate tokens on logout
        bool                       is_active;
    };
    SessionManager() : databaseController(Store::getObject<DatabaseController>()) {}

    virtual ~SessionManager() = default;
    std::optional<std::string> setNowLoginTime(uint64_t id, const std::string &group);
    void                       setNowLogoutTime(uint64_t id, const std::string &group);
    std::optional<std::string> getLastLoginTime(uint64_t id, const std::string &group);
    bool                       getLastLogoutTimeIfActive(LoggedClientInfo &loggedClientInfo);

   private:
    std::shared_ptr<DatabaseController> databaseController;
    std::string                         current_time_to_utc_string();
};
