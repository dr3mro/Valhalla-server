#pragma once
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/kazuho-picojson/traits.h>

#include <optional>

#include "controllers/databasecontroller/databasecontroller.hpp"
#include "utils/sessionmanager/sessionmanager.hpp"

class TokenManager
{
   public:
    using LoggedUserInfo = struct LoggedUserInfo
    {
        std::optional<std::string> token;
        std::optional<std::string> userName;
        std::optional<std::string> group;
        std::optional<uint64_t>    userID;
        std::optional<std::string> llodt;  // used to invalidate tokens on logout
    };

    TokenManager()
    {
        databaseController = Store::getObject<DatabaseController>();
        sessionManager     = Store::getObject<SessionManager>();
    }
    virtual ~TokenManager() = default;

    std::optional<std::string> GenerateToken(const LoggedUserInfo &loggedinUserInfo) const;
    bool                       ValidateToken(LoggedUserInfo &loggedinUserInfo) const;

   private:
    std::shared_ptr<Configurator>               configurator_           = Store::getObject<Configurator>();
    const Configurator::TokenManagerParameters &tokenManagerParameters_ = configurator_->get<Configurator::TokenManagerParameters>();

    std::shared_ptr<DatabaseController>                             databaseController;
    std::shared_ptr<SessionManager>                                 sessionManager;
    jwt::verifier<jwt::default_clock, jwt::traits::kazuho_picojson> createTokenVerifier(const LoggedUserInfo &loggedinUserInfo) const;
    void fillUserInfo(LoggedUserInfo &loggedinUserInfo, const jwt::decoded_jwt<jwt::traits::kazuho_picojson> &token) const;
    bool validateUserInDatabase(const LoggedUserInfo &loggedinUserInfo) const;
};