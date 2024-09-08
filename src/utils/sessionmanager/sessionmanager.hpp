#pragma once

#include <memory>
#include <string>

#include "controllers/databasecontroller/databasecontroller.hpp"
#include "store/store.hpp"

class SessionManager
{
   public:
    SessionManager() { databaseController = Store::getObject<DatabaseController>(); }

    virtual ~SessionManager() = default;
    void                       setNowLoginTime(uint64_t id, const std::string &group);
    void                       setNowLogoutTime(uint64_t id, const std::string &group);
    std::optional<std::string> getLastLoginTime(uint64_t id, const std::string &group);
    std::optional<std::string> getLastLogoutTime(uint64_t id, const std::string &group);

   private:
    std::shared_ptr<DatabaseController> databaseController;
    std::string                         current_time_to_utc_string();
};
