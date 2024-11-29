#include "tokenmanager.hpp"

#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/kazuho-picojson/traits.h>
using namespace api::v2;

bool TokenManager::generateToken(std::optional<Types::ClientLoginData> &clientLoginData)
{
    try
    {
        // Create JWT token with payload
        std::optional<std::string> token =
            jwt::create<jwt::traits::kazuho_picojson>()
                .set_issuer(std::string(tokenManagerParameters_.issuer))
                .set_type(std::string(tokenManagerParameters_.type))
                .set_subject(clientLoginData->username.value())
                .set_id(std::to_string(clientLoginData->clientId.value()))
                .set_issued_at(std::chrono::system_clock::now())
                .set_expires_at(std::chrono::system_clock::now() + std::chrono::minutes{tokenManagerParameters_.validity})
                .set_payload_claim("llodt", jwt::basic_claim<jwt::traits::kazuho_picojson>(clientLoginData->lastLogoutTime.value()))
                .set_payload_claim("group", jwt::basic_claim<jwt::traits::kazuho_picojson>(clientLoginData->group.value()))
                .sign(jwt::algorithm::hs256{std::string(tokenManagerParameters_.secret)});

        if (token.has_value())
        {
            clientLoginData->token = token.value();
            return true;
        }
    }
    catch (const std::exception &e)
    {
        Message::CriticalMessage(fmt::format("Error generating token:{}", e.what()));
    }
    return false;
}
bool TokenManager::isTokenValid(std::optional<Types::ClientLoginData> &clientLoginData, std::string &message)
{
    try
    {
        jwt::decoded_jwt<jwt::traits::kazuho_picojson> decodedToken = jwt::decode<jwt::traits::kazuho_picojson>(clientLoginData->token.value());
        if (!decodeToken(clientLoginData, message, decodedToken))
        {
            message = fmt::format("Token is not valid: {}", message);
            return false;
        }

        // Token is valid
        return validateToken(clientLoginData, message, decodedToken);
    }
    catch (const std::exception &e)
    {
        message = fmt::format("Error validating Token:{}", e.what());
        Message::CriticalMessage(message);
    }
    return false;
}
