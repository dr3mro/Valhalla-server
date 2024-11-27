#include "tokenmanager.hpp"

#include "utils/message/message.hpp"

using jwt::error::token_verification_exception;

std::optional<std::string> TokenManager::GenerateToken(const LoggedClientInfo &loggedinClientInfo) const
{
    try
    {
        // Create JWT token with payload
        std::optional<std::string> token =
            jwt::create<jwt::traits::kazuho_picojson>()
                .set_issuer(tokenManagerParameters_.issuer.data())
                .set_type(tokenManagerParameters_.type.data())
                .set_subject(loggedinClientInfo.userName.value())
                .set_id(std::to_string(loggedinClientInfo.clientId.value()))
                .set_issued_at(std::chrono::system_clock::now())
                .set_expires_at(std::chrono::system_clock::now() + std::chrono::minutes{tokenManagerParameters_.validity})
                .set_payload_claim("llodt", jwt::basic_claim<jwt::traits::kazuho_picojson>(loggedinClientInfo.llodt.value()))
                .set_payload_claim("group", jwt::basic_claim<jwt::traits::kazuho_picojson>(loggedinClientInfo.group.value()))
                .sign(jwt::algorithm::hs256{tokenManagerParameters_.secret.data()});

        return token;
    }
    catch (const std::exception &e)
    {
        Message::ErrorMessage("Error generating token.");
        Message::CriticalMessage(e.what());
    }
    return std::nullopt;
}

bool TokenManager::ValidateToken(LoggedClientInfo &loggedinClientInfo) const
{
    try
    {
        auto token = jwt::decode<jwt::traits::kazuho_picojson>(loggedinClientInfo.token.value());

        // Validate token expiration
        auto now      = std::chrono::system_clock::now().time_since_epoch();
        auto exp_time = token.get_payload_claim("exp").as_date().time_since_epoch();
        if (now >= exp_time)
        {
            return false;
        }

        // Update user info from token
        fillUserInfo(loggedinClientInfo, token);

        // Validate token claims
        auto verifier = createTokenVerifier(loggedinClientInfo);
        verifier.verify(token);

        return true;
    }
    catch (const token_verification_exception &e)
    {
        Message::ErrorMessage("Token verification failed.");
        Message::CriticalMessage(e.what());
    }
    catch (const std::exception &e)
    {
        Message::ErrorMessage("Error validating token.");
        Message::CriticalMessage(e.what());
    }
    return false;
}

jwt::verifier<jwt::default_clock, jwt::traits::kazuho_picojson> TokenManager::createTokenVerifier(const LoggedClientInfo &loggedinClientInfo) const
{
    return jwt::verify<jwt::traits::kazuho_picojson>()
        .allow_algorithm(jwt::algorithm::hs256{tokenManagerParameters_.secret.data()})
        .with_issuer(tokenManagerParameters_.issuer.data())
        .with_type(tokenManagerParameters_.type.data())
        .with_subject(loggedinClientInfo.userName.value())
        .with_id(std::to_string(loggedinClientInfo.clientId.value()))
        .with_claim("group", jwt::basic_claim<jwt::traits::kazuho_picojson>(loggedinClientInfo.group.value()))
        .with_claim("llodt", jwt::basic_claim<jwt::traits::kazuho_picojson>(loggedinClientInfo.llodt.value()));
}

void TokenManager::fillUserInfo(LoggedClientInfo &loggedinClientInfo, const jwt::decoded_jwt<jwt::traits::kazuho_picojson> &token) const
{
    if (!loggedinClientInfo.group)
    {
        loggedinClientInfo.group = token.get_payload_claim("group").as_string();
    }
    else if (loggedinClientInfo.group != token.get_payload_claim("group").as_string())
    {
        throw std::runtime_error("Group mismatch in token");
    }

    loggedinClientInfo.clientId = std::stoull(token.get_id());
    loggedinClientInfo.userName = token.get_subject();

    if (!loggedinClientInfo.group || !loggedinClientInfo.clientId || !loggedinClientInfo.userName)
    {
        throw std::runtime_error("Missing required user information in token");
    }
    // Get last logout time
    loggedinClientInfo.llodt = sessionManager->getLastLogoutTime(loggedinClientInfo.clientId.value(), loggedinClientInfo.group.value());
}

bool TokenManager::validateUserInDatabase(const LoggedClientInfo &loggedinClientInfo) const
{
    return databaseController->findIfUserID(loggedinClientInfo.userName.value(), loggedinClientInfo.group.value()) ==
           loggedinClientInfo.clientId.value();
}
