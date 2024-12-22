#include "gatekeeper/sessionmanager/sessionmanager.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <ctime>
#include <utils/jsonhelper/jsonhelper.hpp>

#include "utils/global/global.hpp"
#include "utils/global/http.hpp"

using SessionManager = api::v2::SessionManager;

bool SessionManager::login(const std::optional<Types::Credentials>& credentials, std::optional<Types::ClientLoginData>& clientLoginData, std::string& message)
{
    std::optional<std::string> password_hash;
    try
    {
        auto client_object = getPasswordHashForUserName(credentials->username, clientLoginData->group.value());

        if (!client_object.has_value() || client_object.value().empty())
        {
            message += "Failure: user might not exist, please try again";
            return false;
        }

        jsoncons::json& client_j = client_object.value();

        clientLoginData->clientId = client_j.at("id").as<uint64_t>();

        if (!clientLoginData->clientId.has_value())
        {
            message += "Failed to find client id from database, please try again";
            return false;
        }

        clientLoginData->username  = credentials->username;
        clientLoginData->is_active = client_j.at("active").as<bool>();

        if (!clientLoginData->is_active)
        {
            message += fmt::format("username: [{}] is suspended, please contact the administrator", credentials->username);
            return false;
        }

        bool success = setNowLoginTimeGetLastLogoutTime(clientLoginData);

        if (!success)
        {
            message +=
                "Failed to set now login and get last logout times , please "
                "try again";
            return false;
        }

        password_hash = client_j.at("password").as_string();

        if (!password_hash.has_value())
        {
            message += "Failed to find password hash from database, please try again";
            return false;
        }

        if (!passwordCrypt->verifyPassword(credentials->password, password_hash.value()))
        {
            message += "Invalid username/password, please try again";
            return false;
        }
    }
    catch (const std::exception& e)
    {
        message += fmt::format("Error: {}", e.what());
        return false;
    }
    return true;
}

void SessionManager::logout(CALLBACK_&& callback, std::optional<Types::ClientLoginData>& clientLoginData)
{
    try
    {
        auto        decodedToken = jwt::decode<jwt::traits::kazuho_picojson>(clientLoginData->token.value());
        std::string message;

        if (!decodeToken(clientLoginData, message, decodedToken))
        {
            std::move(callback)(Http::Status::OK, JsonHelper::stringify(JsonHelper::jsonify(message)));
            return;
        }

        // bool status = tokenManager->ValidateToken(loggedClientInfo);
        // if (!status)
        // {
        //     callback(api::v2::Http::Status::UNAUTHORIZED, "Logout failure.");
        //     Message::ErrorMessage("Logout failure.");
        //     return;
        // }
        setNowLogoutTime(clientLoginData->clientId.value(), clientLoginData->group.value());
        removeSession(clientLoginData);
        std::move(callback)(api::v2::Http::Status::OK, api::v2::JsonHelper::stringify(api::v2::JsonHelper::jsonify("Logout success.")));
    }
    catch (const std::exception& e)
    {
        std::move(callback)(api::v2::Http::Status::INTERNAL_SERVER_ERROR, e.what());
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
            return true;
        }
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
    catch (const std::exception& e)
    {
        message = fmt::format("Error storing session:{}", e.what());
        Message::CriticalMessage(message);
    }
    return false;
}
