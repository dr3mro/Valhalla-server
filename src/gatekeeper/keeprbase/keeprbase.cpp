#include "gatekeeper/keeprbase/keeprbase.hpp"

using namespace api::v2;

bool KeeprBase::getLastLogoutTimeIfActive(std::optional<Types::ClientLoginData>& clientLoginData)
{
    try
    {
        std::string query = fmt::format(
            "WITH client_data AS (SELECT ses.id, ses.last_logout, client.active FROM {}_sessions ses\
            LEFT JOIN {} client ON ses.id = client.id) \
            SELECT id,\
            last_logout, active FROM client_data WHERE id = {};",
            clientLoginData->group.value(), clientLoginData->group.value(), clientLoginData->clientId.value());

        auto results = databaseController->executeReadQuery(query);
        if (results.has_value())
        {
            if (!results.value().empty())
            {
                clientLoginData->is_active      = results.value().at("active").as_bool();
                clientLoginData->lastLogoutTime = results.value().at("last_logout").as_string();
                return true;
            }
        }
        return false;
    }
    catch (std::exception& e)
    {
        CRITICALMESSAGE
    }
    return false;
}

bool KeeprBase::setNowLoginTimeGetLastLogoutTime(std::optional<Types::ClientLoginData>& clientLoginData)
{
    try
    {
        clientLoginData->nowLoginTime = current_time_to_utc_string();

        if (!clientLoginData->group.has_value() || !clientLoginData->clientId.has_value())
        {
            Message::ErrorMessage("group or client id is empty");
            return false;
        }
        std::string query = fmt::format(
            "INSERT INTO {}_sessions (id, last_login,last_logout) VALUES ({}, '{}', '{}') "
            "ON CONFLICT (id) DO UPDATE SET last_login = EXCLUDED.last_login RETURNING last_logout;",
            clientLoginData->group.value(), clientLoginData->clientId.value(), clientLoginData->nowLoginTime.value(),
            clientLoginData->nowLoginTime.value());

        auto result = databaseController->executeQuery(query);
        if (!result.has_value())
        {
            Message::ErrorMessage("Error updating login time.");
            Message::CriticalMessage(result.value().to_string());
            return false;
        }
        else if (!result->empty())
        {
            clientLoginData->lastLogoutTime = result->at("last_logout").as_string();
        }
        return true;
    }
    catch (const std::exception& e)
    {
        Message::ErrorMessage("Error updating login time.");
        Message::CriticalMessage(e.what());
    }
    return false;
}

void KeeprBase::setNowLogoutTime(uint64_t id, const std::string& group)
{
    try
    {
        std::string logout_time = current_time_to_utc_string();

        std::string query = fmt::format(
            "INSERT INTO {}_sessions (id, last_logout) VALUES ({}, '{}') "
            "ON CONFLICT (id) DO UPDATE SET last_logout = EXCLUDED.last_logout;",
            group, id, logout_time);

        auto result = databaseController->executeQuery(query);
        if (!result.has_value())
        {
            Message::ErrorMessage("Error updating login time.");
            Message::CriticalMessage(result.value().to_string());
        }
    }
    catch (const std::exception& e)
    {
        Message::ErrorMessage("Error updating logout time.");
        Message::CriticalMessage(e.what());
    }
}

std::optional<std::string> KeeprBase::getLastLoginTime(uint64_t id, const std::string& group)
{
    std::string query = fmt::format("SELECT last_login FROM {}_sessions WHERE id = {};", group, id);
    return databaseController->doReadQuery(query);
}

std::string KeeprBase::current_time_to_utc_string()
{
    // Get current time as a time_point
    auto now = std::chrono::system_clock::now();

    // Convert time_point to time_t
    auto now_t = std::chrono::system_clock::to_time_t(now);

    // Convert time_t to tm structure in UTC
    std::tm tm = *std::gmtime(&now_t);

    // Format the time using fmt::format
    std::string formatted_time =
        fmt::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02} +0000", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

    return formatted_time;
}

bool KeeprBase::decodeToken(std::optional<Types::ClientLoginData>& clientLoginData, std::string& message,
                            jwt::decoded_jwt<jwt::traits::kazuho_picojson>& decoedToken)
{
    try
    {
        // Validate token expiration
        auto now                    = std::chrono::system_clock::now().time_since_epoch();
        clientLoginData->expireTime = decoedToken.get_payload_claim("exp").as_date().time_since_epoch();

        if (now >= clientLoginData->expireTime)
        {
            message = "Token expired";
            return false;
        }

        if (clientLoginData->group != decoedToken.get_payload_claim("group").as_string())
        {
            message = "Token group does not match user group";
            return false;
        }

        clientLoginData->clientId = std::stoull(decoedToken.get_id());
        clientLoginData->username = decoedToken.get_subject();

        return true;
    }
    catch (const std::exception& e)
    {
        message = fmt::format("Error storing session:{}", e.what());
        Message::CriticalMessage(message);
    }
    return false;
}
bool KeeprBase::validateToken(std::optional<Types::ClientLoginData>& clientLoginData, std::string& message,
                              jwt::decoded_jwt<jwt::traits::kazuho_picojson>& decodedToken)
{
    try
    {
        getLastLogoutTimeIfActive(clientLoginData);
        if (clientLoginData->lastLogoutTime != decodedToken.get_payload_claim("llodt").as_string())
        {
            message = "Client is logged out from server, Please login again.";
            return false;
        }

        if (!clientLoginData->is_active)
        {
            message = "Account is suspened, please contact the administrator.";
            return false;
        }

        // Validate token claims
        auto verifier = createTokenVerifier(clientLoginData);

        verifier.verify(decodedToken);

        // Token is valid
        return true;
    }
    catch (const jwt::error::token_verification_exception& e)
    {
        message = fmt::format("Token verification failed:{}", e.what());
        Message::CriticalMessage(message);
        return false;
    }
    catch (const std::exception& e)
    {
        message = fmt::format("Error validating Token:{}", e.what());
        Message::CriticalMessage(message);
        return false;
    }
    return false;
}

jwt::verifier<jwt::default_clock, jwt::traits::kazuho_picojson> KeeprBase::createTokenVerifier(
    const std::optional<Types::ClientLoginData>& clientLoginData)
{
    if (!clientLoginData || !clientLoginData->username || !clientLoginData->clientId || !clientLoginData->group)
    {
        throw std::runtime_error("Invalid client login data");
    }

    return jwt::verify<jwt::traits::kazuho_picojson>()
        .allow_algorithm(jwt::algorithm::hs256{std::string(tokenManagerParameters_.secret)})
        .with_issuer(std::string(tokenManagerParameters_.issuer))
        .with_type(std::string(tokenManagerParameters_.type))
        .with_subject(clientLoginData->username.value())
        .with_id(std::to_string(clientLoginData->clientId.value()))
        .with_claim("group", jwt::basic_claim<jwt::traits::kazuho_picojson>(clientLoginData->group.value()))
        .with_claim("llodt", jwt::basic_claim<jwt::traits::kazuho_picojson>(clientLoginData->lastLogoutTime.value()));
}
