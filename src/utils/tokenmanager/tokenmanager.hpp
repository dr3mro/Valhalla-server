#pragma once
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/kazuho-picojson/traits.h>

#include <optional>

#include "controllers/databasecontroller/databasecontroller.hpp"
#include "utils/sessionmanager/sessionmanager.hpp"
/**
 * @class TokenManager
 * @brief Manages the generation and validation of JWT tokens for user
 * authentication.
 *
 * The TokenManager class is responsible for generating and validating JWT
 * tokens for user authentication. It uses the jwt-cpp library to handle the
 * token creation and verification.
 *
 * The TokenManager class has the following main functions:
 * - `GenerateToken`: Generates a JWT token for a given user information.
 * - `ValidateToken`: Validates a JWT token and extracts the user information
 * from it.
 *
 * The TokenManager class also has a `TokenManagerParameters` struct that holds
 * the configuration parameters for the token management, such as the issuer,
 * type, validity, and secret.
 *
 * The TokenManager class uses the `DatabaseController` and `SessionManager`
 * classes to interact with the database and session management, respectively.
 */
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