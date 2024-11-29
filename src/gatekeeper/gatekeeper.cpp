#include "gatekeeper.hpp"

#include <cstdint>

#include "entities/base/types.hpp"
#include "entities/people/provider.hpp"
#include "entities/people/user.hpp"
#include "utils/global/global.hpp"

using namespace api::v2;

void GateKeeper::login(CALLBACK_&& callback, std::string_view data, std::string_view group)
{
    try
    {
        bool                              success     = false;
        std::optional<Types::Credentials> credentials = parse_credentials(std::move(callback), data, success);

        if (!success || !credentials.has_value())
        {
            callback(api::v2::Http::Status::BAD_REQUEST, "credentials parse error");
            return;
        }
        std::optional<Types::ClientLoginData> clientLoginData = Types::ClientLoginData{};
        clientLoginData->group                                = group;

        success = sessionManager_->login(std::move(callback), credentials, clientLoginData);

        if (!success || !clientLoginData.has_value())
        {
            callback(Http::Status::UNAUTHORIZED, "login failed");
            return;
        }

        success = tokenManager_->generateToken(clientLoginData);
        if (!success)
        {
            callback(Http::Status::INTERNAL_SERVER_ERROR, "failed to generate token");
            return;
        }

        jsoncons::json token_object;
        token_object["token"]     = clientLoginData->token;
        token_object["username"]  = clientLoginData->username;
        token_object["client_id"] = clientLoginData->clientId;
        token_object["group"]     = clientLoginData->group;

        callback(Http::Status::OK, token_object.as<std::string>());
    }
    catch (const std::exception& e)
    {
        CRITICALMESSAGERESPONSE
    }
}

void GateKeeper::logout(CALLBACK_&& callback, const std::optional<std::string>& token, const std::string& group)
{
    try
    {
        std::optional<Types::ClientLoginData> clientLoginData = Types::ClientLoginData{};
        clientLoginData->token                                = token;
        clientLoginData->group                                = group;
        sessionManager_->logout(std::move(callback), clientLoginData);
    }
    catch (const std::exception& e)
    {
        CRITICALMESSAGERESPONSE
    }
}

bool GateKeeper::isAuthenticationValid(std::optional<Types::ClientLoginData>& clientLoginData, std::string& message)
{
    if (sessionManager_->clientHasValidSession(clientLoginData, message))
    {
        std::cout << "Session is valid" << std::endl;
        return true;
    }
    else if (tokenManager_->isTokenValid(clientLoginData, message))
    {
        std::cout << "Token is valid" << std::endl;
        std::cout << "storing session" << std::endl;
        return sessionManager_->storeSession(clientLoginData, message);
    }
    return false;
}

void GateKeeper::removeSession(std::optional<uint64_t> client_id, const std::string& group)
{
    std::optional<Types::ClientLoginData> clientLoginData = Types::ClientLoginData{};
    clientLoginData->clientId                             = client_id;
    clientLoginData->group                                = group;
    sessionManager_->removeSession(clientLoginData);
}
// bool GateKeeper::hasPermission(CALLBACK_&& callback, std::string_view data) { return false; }

// bool GateKeeper::isDosAttack(CALLBACK_&& callback, std::string_view data) { return false; }

std::optional<jsoncons::json> GateKeeper::parse_data(CALLBACK_&& callback, std::string_view data, bool& success)
{
    success = false;
    jsoncons::json j;
    try
    {
        j = jsoncons::json::parse(data);
    }
    catch (const std::exception& e)
    {
        CRITICALMESSAGERESPONSE
    }
    success = true;
    return j;
}

std::optional<Types::Credentials> GateKeeper::parse_credentials(CALLBACK_&& callback, std::string_view data, bool& success)
{
    success = false;

    std::optional<jsoncons::json> credentials_j = parse_data(std::move(callback), data, success);
    if (!success || !credentials_j.has_value())
        return std::nullopt;

    Types::Credentials credentials;
    try
    {
        credentials.username = credentials_j.value().at("username").as<std::string>();
        credentials.password = credentials_j.value().at("password").as<std::string>();
    }
    catch (const std::exception& e)
    {
        CRITICALMESSAGERESPONSE
    }
    success = true;
    return credentials;
}
