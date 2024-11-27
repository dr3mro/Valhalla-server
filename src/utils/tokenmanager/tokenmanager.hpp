#pragma once
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/kazuho-picojson/traits.h>

#include <optional>

#include "controllers/databasecontroller/databasecontroller.hpp"
#include "utils/sessionmanager/sessionmanager.hpp"

class TokenManager
{
   public:
    TokenManager() : databaseController(Store::getObject<DatabaseController>()), sessionManager(Store::getObject<SessionManager>()) {}
    virtual ~TokenManager() = default;

    std::optional<std::string> GenerateToken(const SessionManager::LoggedClientInfo &loggedinClientInfo) const;
    bool                       ValidateToken(SessionManager::LoggedClientInfo &loggedinClientInfo) const;

   private:
    std::shared_ptr<Configurator>               configurator_           = Store::getObject<Configurator>();
    const Configurator::TokenManagerParameters &tokenManagerParameters_ = configurator_->get<Configurator::TokenManagerParameters>();

    std::shared_ptr<DatabaseController>                             databaseController;
    std::shared_ptr<SessionManager>                                 sessionManager;
    jwt::verifier<jwt::default_clock, jwt::traits::kazuho_picojson> createTokenVerifier(
        const SessionManager::LoggedClientInfo &loggedinClientInfo) const;
    void fillUserInfo(SessionManager::LoggedClientInfo &loggedinClientInfo, const jwt::decoded_jwt<jwt::traits::kazuho_picojson> &token) const;
    bool validateUserInDatabase(const SessionManager::LoggedClientInfo &loggedinClientInfo) const;
};