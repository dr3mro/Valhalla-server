#include "gatekeeper/keeprbase/keeprbase.hpp"

#include <fmt/core.h>
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/kazuho-picojson/traits.h>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <exception>
#include <jsoncons/basic_json.hpp>
#include <optional>
#include <pqxx/pqxx>
#include <stdexcept>
#include <string>

#include "database/databasebusy.hpp"
#include "gatekeeper/types.hpp"
#include "utils/global/global.hpp"
#include "utils/message/message.hpp"
using KeeprBase = api::v2::KeeprBase;

namespace
{
    // Identifiers cannot be bound as parameters, so the session table is chosen from a fixed allowlist.
    std::optional<std::string> sessionsTable(const std::string& group)
    {
        if (group == "users" || group == "providers")
        {
            return group + "_sessions";
        }
        return std::nullopt;
    }
}  // namespace

bool KeeprBase::getLastLogoutTimeIfActive(std::optional<Types::ClientLoginData>& clientLoginData)
{
    try
    {
        const auto sessions = sessionsTable(clientLoginData->group.value());
        if (!sessions.has_value())
        {
            Message::ErrorMessage("Unknown client group.");
            return false;
        }
        const std::string param_query = fmt::format(
            "WITH client_data AS (SELECT ses.id, ses.last_logout, client.active FROM {} ses "
            "LEFT JOIN {} client ON ses.id = client.id) "
            "SELECT id, last_logout, active FROM client_data WHERE id = $1;",
            sessions.value(), clientLoginData->group.value());
        pqxx::params params;
        params.append(clientLoginData->clientId.value());

        auto results = databaseController()->executeReadQuery(param_query, params);

        if (results.has_value())
        {
            if (!results->empty())
            {
                clientLoginData->is_active      = results.value().at("active").as_bool();
                clientLoginData->lastLogoutTime = results.value().at("last_logout").as_string();
                return true;
            }
        }
        return false;
    }
    catch (const DatabaseBusy&)
    {
        throw;
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
        const auto sessions = sessionsTable(clientLoginData->group.value());
        if (!sessions.has_value())
        {
            Message::ErrorMessage("Unknown client group.");
            return false;
        }
        const std::string param_query = fmt::format(
            "INSERT INTO {} (id, last_login, last_logout) VALUES ($1, $2, $2) "
            "ON CONFLICT (id) DO UPDATE SET last_login = EXCLUDED.last_login "
            "RETURNING last_logout;",
            sessions.value());
        pqxx::params params;
        params.append(clientLoginData->clientId.value());
        params.append(clientLoginData->nowLoginTime.value());

        auto result = databaseController()->executeQuery(param_query, params);

        if (!result.has_value())
        {
            Message::ErrorMessage("Error updating login time.");
            return false;
        }

        if (!result->empty())
        {
            clientLoginData->lastLogoutTime = result->at("last_logout").as_string();
        }
        return true;
    }
    catch (const DatabaseBusy&)
    {
        throw;
    }
    catch (const std::exception& e)
    {
        Message::ErrorMessage("Error updating login time.");
        Message::CriticalMessage(e.what());
    }
    return false;
}

void KeeprBase::setNowLogoutTime(uint64_t _id, const std::string& _group)
{
    try
    {
        std::string logout_time = current_time_to_utc_string();

        const auto sessions = sessionsTable(_group);
        if (!sessions.has_value())
        {
            Message::ErrorMessage("Unknown client group.");
            return;
        }
        const std::string param_query = fmt::format(
            "INSERT INTO {} (id, last_logout) VALUES ($1, $2) "
            "ON CONFLICT (id) DO UPDATE SET last_logout = EXCLUDED.last_logout;",
            sessions.value());
        pqxx::params params;
        params.append(_id);
        params.append(logout_time);

        auto result = databaseController()->executeQuery(param_query, params);

        if (!result.has_value())
        {
            Message::CriticalMessage("Error updating login time.");
        }
    }
    catch (const DatabaseBusy&)
    {
        throw;
    }
    catch (const std::exception& e)
    {
        Message::ErrorMessage("Error updating logout time.");
        Message::CriticalMessage(e.what());
    }
}

std::optional<std::string> KeeprBase::getLastLoginTime(uint64_t _id, const std::string& _group)
{
    const auto sessions = sessionsTable(_group);
    if (!sessions.has_value())
    {
        Message::ErrorMessage("Unknown client group.");
        return std::nullopt;
    }
    const std::string param_query = fmt::format("SELECT last_login FROM {} WHERE id = $1;", sessions.value());
    pqxx::params      params;
    params.append(_id);

    return databaseController()->doReadQuery(param_query, params);
}

std::string KeeprBase::current_time_to_utc_string()
{
    constexpr int YEAR_OFFSET = 1900;
    // Get current time as a time_point
    auto now = std::chrono::system_clock::now();

    // Convert time_point to time_t
    auto now_t = std::chrono::system_clock::to_time_t(now);

    // Convert time_t to tm structure in UTC
    std::tm mtime{};
    if (gmtime_r(&now_t, &mtime) == nullptr)
    {
        throw std::runtime_error("Failed to convert time to UTC");
    }

    // Format the time using fmt::format
    std::string formatted_time = fmt::format(
        "{:04}-{:02}-{:02} {:02}:{:02}:{:02} +0000", mtime.tm_year + YEAR_OFFSET, mtime.tm_mon + 1, mtime.tm_mday, mtime.tm_hour, mtime.tm_min, mtime.tm_sec);

    return formatted_time;
}

bool KeeprBase::decodeToken(
    std::optional<Types::ClientLoginData>& clientLoginData, std::string& message, jwt::decoded_jwt<jwt::traits::kazuho_picojson>& decoedToken)
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

        if (clientLoginData->ip_address != decoedToken.get_payload_claim("ip_address").as_string())
        {
            message = "Token ip address does not match user ip address";
            return false;
        }

        clientLoginData->group    = decoedToken.get_payload_claim("group").as_string();
        clientLoginData->clientId = std::stoull(decoedToken.get_id());
        clientLoginData->username = decoedToken.get_subject();

        return true;
    }
    catch (const DatabaseBusy&)
    {
        throw;
    }
    catch (const std::exception& e)
    {
        message = fmt::format("Error storing session:{}", e.what());
        Message::CriticalMessage(message);
    }
    return false;
}
bool KeeprBase::validateToken(
    std::optional<Types::ClientLoginData>& clientLoginData, std::string& message, jwt::decoded_jwt<jwt::traits::kazuho_picojson>& decodedToken)
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
            message = "Account is suspend, please contact the administrator.";
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
    catch (const DatabaseBusy&)
    {
        throw;
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
    const std::optional<Types::ClientLoginData>& clientLoginData) const
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
        .with_claim("ip_address", jwt::basic_claim<jwt::traits::kazuho_picojson>(clientLoginData->ip_address.value()))
        .with_claim("group", jwt::basic_claim<jwt::traits::kazuho_picojson>(clientLoginData->group.value()))
        .with_claim("llodt", jwt::basic_claim<jwt::traits::kazuho_picojson>(clientLoginData->lastLogoutTime.value()));
}

std::optional<jsoncons::json> api::v2::KeeprBase::getPasswordHashForUserName(const std::string& username, const std::string& _group)
{
    return databaseController()->getPasswordHashForUserName(username, _group);
}

drogon::Task<std::optional<jsoncons::json>> api::v2::KeeprBase::getPasswordHashForUserNameCoro(const std::string& username, const std::string& _group)
{
    auto asyncClient = Store::getObject<AsyncDbClient>(nullptr); // unused; delegate
    co_return co_await databaseController()->getPasswordHashForUserNameCoro(username, _group);
}
