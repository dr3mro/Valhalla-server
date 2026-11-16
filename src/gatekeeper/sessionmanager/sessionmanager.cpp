#include "gatekeeper/sessionmanager/sessionmanager.hpp"
#include "drogon/utils/coroutine.h"

#include <fmt/core.h>
#include <fmt/format.h>
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/kazuho-picojson/traits.h>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <exception>
#include <jsoncons/basic_json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <utils/jsonhelper/jsonhelper.hpp>

#include "database/databasebusy.hpp"
#include "gatekeeper/types.hpp"
#include "utils/global/callback.hpp"
#include "utils/global/global.hpp"
#include "utils/global/http.hpp"
#include "utils/message/message.hpp"

using SessionManager = api::v2::SessionManager;

// Login is split in two so the slow part (password hashing) can run on the password worker:
//   fetchPasswordHash  - reads the stored hash and checks the account is active (database reads only, no writes)
//   completeLogin      - verifies the password and only then records the login time
// login() runs both on the calling thread.
std::optional<std::string> SessionManager::fetchPasswordHash(
    const std::optional<Types::Credentials>& credentials, std::optional<Types::ClientLoginData>& clientLoginData, std::string& message)
{
    try
    {
        auto client_object = getPasswordHashForUserName(credentials->username, clientLoginData->group.value());

        if (!client_object.has_value() || client_object.value().empty())
        {
            message = fmt::format("Failure: username {} does not exist, please try again.", credentials->username);
            return std::nullopt;
        }

        jsoncons::json& client_j = client_object.value();

        clientLoginData->clientId = client_j.at("id").as<uint64_t>();

        if (!clientLoginData->clientId.has_value())
        {
            message += "Failed to find client id from database, please try again";
            return std::nullopt;
        }

        clientLoginData->username  = credentials->username;
        clientLoginData->is_active = client_j.at("active").as<bool>();

        if (!clientLoginData->is_active)
        {
            message += fmt::format("username: [{}] is suspended, please contact the administrator", credentials->username);
            return std::nullopt;
        }

        std::optional<std::string> password_hash = client_j.at("password").as_string();

        if (!password_hash.has_value())
        {
            message += "Failed to find password hash from database, please try again";
            return std::nullopt;
        }
        return password_hash;
    }
    catch (const DatabaseBusy&)
    {
        throw;
    }
    catch (const std::exception& e)
    {
        message += fmt::format("Error: {}", e.what());
    }
    return std::nullopt;
}

drogon::Task<std::optional<std::string>> SessionManager::fetchPasswordHashCoro(
    const std::optional<Types::Credentials>& credentials, std::optional<Types::ClientLoginData>& clientLoginData, std::string& message)
{
    try
    {
        auto client_object = co_await getPasswordHashForUserNameCoro(credentials->username, clientLoginData->group.value());

        if (!client_object.has_value() || client_object.value().empty())
        {
            message = fmt::format("Failure: username {} does not exist, please try again.", credentials->username);
            co_return std::nullopt;
        }

        jsoncons::json& client_j = client_object.value();

        clientLoginData->clientId = client_j.at("id").as<uint64_t>();

        if (!clientLoginData->clientId.has_value())
        {
            message += "Failed to find client id from database, please try again";
            co_return std::nullopt;
        }

        clientLoginData->username  = credentials->username;
        clientLoginData->is_active = client_j.at("active").as<bool>();

        if (!clientLoginData->is_active)
        {
            message += fmt::format("username: [{}] is suspended, please contact the administrator", credentials->username);
            co_return std::nullopt;
        }

        std::optional<std::string> password_hash = client_j.at("password").as_string();

        if (!password_hash.has_value())
        {
            message += "Failed to find password hash from database, please try again";
            co_return std::nullopt;
        }
        co_return password_hash;
    }
    catch (const DatabaseBusy&)
    {
        throw;
    }
    catch (const std::exception& e)
    {
        message += fmt::format("Error: {}", e.what());
    }
    co_return std::nullopt;
}

bool SessionManager::completeLogin(const std::optional<Types::Credentials>& credentials, const std::string& password_hash,
    std::optional<Types::ClientLoginData>& clientLoginData, std::string& message)
{
    try
    {
        // Check the password first: a failed login must not touch the session row.
        if (!passwordCrypt->verifyPassword(credentials->password, password_hash))
        {
            message += "Invalid username/password, please try again";
            return false;
        }

        if (!setNowLoginTimeGetLastLogoutTime(clientLoginData))
        {
            message +=
                "Failed to set now login and get last logout times , please "
                "try again";
            return false;
        }
    }
    catch (const DatabaseBusy&)
    {
        throw;
    }
    catch (const std::exception& e)
    {
        message += fmt::format("Error: {}", e.what());
        return false;
    }
    return true;
}

bool SessionManager::login(const std::optional<Types::Credentials>& credentials, std::optional<Types::ClientLoginData>& clientLoginData, std::string& message)
{
    const std::optional<std::string> password_hash = fetchPasswordHash(credentials, clientLoginData, message);
    if (!password_hash.has_value())
    {
        return false;
    }
    return completeLogin(credentials, password_hash.value(), clientLoginData, message);
}

void SessionManager::logout(CALLBACK_&& callback, std::optional<Types::ClientLoginData>& clientLoginData)
{
    try
    {
        if (!clientLoginData.has_value() || !clientLoginData->token.has_value())
        {
            std::move(callback)(Http::Status::UNAUTHORIZED, JsonHelper::stringify(JsonHelper::jsonify("Missing token.")));
            return;
        }

        std::optional<jwt::decoded_jwt<jwt::traits::kazuho_picojson>> decodedToken;
        std::string message;
        try
        {
            decodedToken = jwt::decode<jwt::traits::kazuho_picojson>(clientLoginData->token.value());
        }
        catch (const std::exception&)
        {
            std::move(callback)(Http::Status::UNAUTHORIZED, JsonHelper::stringify(JsonHelper::jsonify("Invalid token.")));
            return;
        }

        if (!decodeToken(clientLoginData, message, decodedToken.value()))
        {
            std::move(callback)(Http::Status::UNAUTHORIZED, JsonHelper::stringify(JsonHelper::jsonify(message)));
            return;
        }

        // If a session is stored for this (group, client), the supplied token must match it
        // (constant time) to prevent a forged JWT from logging a victim out.
        std::string key = fmt::format("{}_{}", clientLoginData->group.value(), clientLoginData->clientId.value());
        auto        session = clientsSessionsList->get(key);
        if (session.has_value())
        {
            const std::string& stored_token   = session->token.value_or("");
            const std::string& supplied_token = clientLoginData->token.value_or("");
            if (stored_token.empty() || supplied_token.empty() || stored_token.size() != supplied_token.size())
            {
                std::move(callback)(Http::Status::UNAUTHORIZED, JsonHelper::stringify(JsonHelper::jsonify("Invalid token.")));
                return;
            }
            bool tokens_equal = true;
            for (size_t i = 0; i < stored_token.size(); ++i)
            {
                if (stored_token[i] != supplied_token[i])
                {
                    tokens_equal = false;
                }
            }
            if (!tokens_equal)
            {
                std::move(callback)(Http::Status::UNAUTHORIZED, JsonHelper::stringify(JsonHelper::jsonify("Invalid token.")));
                return;
            }
        }

        setNowLogoutTime(clientLoginData->clientId.value(), clientLoginData->group.value());
        removeSession(clientLoginData);
        std::move(callback)(api::v2::Http::Status::OK, api::v2::JsonHelper::stringify(api::v2::JsonHelper::jsonify("Logout success.")));
    }
    catch (const std::exception& e)
    {
        std::move(callback)(api::v2::Http::statusForException(e), e.what());
        CRITICALMESSAGE
    }
}

bool SessionManager::clientHasValidSession(std::optional<Types::ClientLoginData>& clientLoginData, std::string& message)
{
    try
    {
        auto decodedToken = jwt::decode<jwt::traits::kazuho_picojson>(clientLoginData->token.value());

        if (!decodeToken(clientLoginData, message, decodedToken))
        {
            message = fmt::format("Token is not valid: {}", message);
            return false;
        }

        std::string key = fmt::format("{}_{}", clientLoginData->group.value(), clientLoginData->clientId.value());

        auto session = clientsSessionsList->get(key);
        if (session.has_value())
        {
            // Compare the stored token with the supplied one in constant time so a
            // forged JWT that happens to carry a valid (group, client) pair cannot
            // hijack an existing session.
            const std::string& stored_token   = session->token.value_or("");
            const std::string& supplied_token = clientLoginData->token.value_or("");
            if (stored_token.empty() || supplied_token.empty() || stored_token.size() != supplied_token.size())
            {
                return false;
            }
            bool tokens_equal = true;
            for (size_t i = 0; i < stored_token.size(); ++i)
            {
                if (stored_token[i] != supplied_token[i])
                {
                    tokens_equal = false;
                }
            }
            if (!tokens_equal)
            {
                return false;
            }
            // Trust the stored session's identity/expiry, not the (possibly forged) JWT.
            clientLoginData = session.value();
            return true;
        }
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

bool SessionManager::storeSession(std::optional<Types::ClientLoginData>& clientLoginData, std::string& message)
{
    try
    {
        if (!clientLoginData.has_value())
        {
            message = "clientLoginData is empty";
            return false;
        }
        std::string key = fmt::format("{}_{}", clientLoginData->group.value(), clientLoginData->clientId.value());
        clientsSessionsList->insert(key, clientLoginData.value(), std::chrono::duration_cast<std::chrono::seconds>(clientLoginData->expireTime));
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

bool SessionManager::removeSession(std::optional<Types::ClientLoginData>& clientLoginData)
{
    std::string message;
    try
    {
        if (!clientLoginData.has_value())
        {
            message = "clientLoginData is empty";
            return false;
        }
        if (!clientLoginData->clientId.has_value())
        {
            message = "clientId has no value";
            return false;
        }
        if (!clientLoginData->group.has_value())
        {
            message = "group has no value";
            return false;
        }

        std::string key = fmt::format("{}_{}", clientLoginData->group.value(), clientLoginData->clientId.value());
        clientsSessionsList->remove(key);
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
