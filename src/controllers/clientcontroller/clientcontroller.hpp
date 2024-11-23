#pragma once

#include <fmt/core.h>

#include <jsoncons/json.hpp>
#include <memory>
#include <optional>

#include "controllers/clientcontroller/clientcontrollerbase.hpp"
#include "controllers/entitycontroller/entitycontroller.hpp"
#include "entities/base/client.hpp"
#include "entities/base/types.hpp"
#include "utils/global/global.hpp"
#include "utils/jsonhelper/jsonhelper.hpp"
#include "utils/sessionmanager/sessionmanager.hpp"
#include "utils/tokenmanager/tokenmanager.hpp"

template <typename T>
concept Client_t = std::is_base_of_v<Client, T>;

template <Client_t T, typename CALLBACK>
class ClientController : public EntityController<T, CALLBACK>, public ClientControllerBase<CALLBACK>
{
   public:
    ClientController()
    {
        try
        {
            tokenManager   = Store::getObject<TokenManager>();
            sessionManager = Store::getObject<SessionManager>();
        }
        catch (const std::exception& e)
        {
            CRITICALMESSAGE
            exit(EXIT_FAILURE);
        }
    }

    virtual ~ClientController() final = default;
    void                    Create(CALLBACK&& callback, std::string_view data) final;
    void                    Read(CALLBACK&& callback, std::string_view data) final;
    void                    Update(CALLBACK&& callback, std::string_view data, std::optional<uint64_t> id) final;
    void                    Delete(CALLBACK&& callback, std::optional<uint64_t> client_id) final;
    void                    Search(CALLBACK&& callback, std::string_view data) final;
    std::optional<uint64_t> Login(CALLBACK&& callback, std::string_view data) final;
    void                    Logout(CALLBACK&& callback, const std::optional<std::string>& token) final;
    void                    Suspend(CALLBACK&& callback, std::optional<uint64_t> client_id) final;
    void                    Activate(CALLBACK&& callback, std::optional<uint64_t> client_id) final;
    void                    ResetPassword(CALLBACK&& callback, std::string_view data) final;
    void                    GetServices(CALLBACK&& callback, std::optional<uint64_t> client_id) final;

   private:
    std::shared_ptr<TokenManager>   tokenManager;
    std::shared_ptr<SessionManager> sessionManager;
};

template <Client_t T, typename CALLBACK>
void ClientController<T, CALLBACK>::Create(CALLBACK&& callback, std::string_view data)
{
    try
    {
        bool                            success = false;
        api::v2::Global::HttpError      error;
        std::unordered_set<std::string> exclude;
        jsoncons::json                  json_data = jsoncons::json::parse(data);

        Types::ClientData client_data(data, std::nullopt, error, success, T::getTableName(), exclude);

        if (success)
        {
            T client(client_data);
            if (client.exists())
            {
                callback(409, "User already exists");
                return;
            }
            Controller::Create(client, std::move(callback));
        }
        else
        {
            callback(error.code, fmt::format("ClientData parsing error: {}.", error.message));
            return;
        }
    }
    catch (const std::exception& e)
    {
        CRITICALMESSAGERESPONSE
    }
}

template <Client_t T, typename CALLBACK>
void ClientController<T, CALLBACK>::Read(CALLBACK&& callback, std::string_view data)
{
    EntityController<T, CALLBACK>::Read(std::move(callback), data);
}

template <Client_t T, typename CALLBACK>
void ClientController<T, CALLBACK>::Update(CALLBACK&& callback, std::string_view data, const std::optional<uint64_t> id)
{
    jsoncons::json json_data;
    try
    {
        json_data                               = jsoncons::json::parse(data);
        bool                            success = false;
        std::unordered_set<std::string> exclude{"password"};
        api::v2::Global::HttpError      error;
        Types::ClientData               client_data(data, id, error, success, T::getTableName(), exclude);

        if (success)
        {
            T client(client_data);
            Controller::Update(client, callback);
        }
        else
        {
            callback(error.code, fmt::format("ClientData parsing error: {}.", error.message));
            return;
        }
    }
    catch (const std::exception& e)
    {
        CRITICALMESSAGERESPONSE
    }
}

template <Client_t T, typename CALLBACK>
void ClientController<T, CALLBACK>::Delete(CALLBACK&& callback, const std::optional<uint64_t> client_id)
{
    EntityController<T, CALLBACK>::Delete(std::move(callback), client_id);
}

template <Client_t T, typename CALLBACK>
void ClientController<T, CALLBACK>::Search(CALLBACK&& callback, std::string_view data)
{
    EntityController<T, CALLBACK>::Search(std::move(callback), data);
}

template <Client_t T, typename CALLBACK>
std::optional<uint64_t> ClientController<T, CALLBACK>::Login(CALLBACK&& callback, std::string_view data)
{
    jsoncons::json          credentials_j;
    std::optional<uint64_t> client_id;
    Types::Credentials      credentials;
    try
    {
        credentials_j = jsoncons::json::parse(data);

        credentials.username = credentials_j.at("username").as<std::string>();
        credentials.password = credentials_j.at("password").as<std::string>();

        T client(credentials);

        client_id = client.authenticate();

        if (!client_id)
        {
            callback(401, fmt::format("User '{}' not found or wrong password", credentials.username));
            return std::nullopt;
        }

        TokenManager::LoggedClientInfo loggedClientInfo;

        loggedClientInfo.userID   = client_id;
        loggedClientInfo.userName = credentials.username;
        loggedClientInfo.group    = client.getGroupName();
        loggedClientInfo.llodt =
            sessionManager->getLastLogoutTime(loggedClientInfo.userID.value(), loggedClientInfo.group.value()).value_or("first_login");

        json token_object;
        token_object["token"]     = tokenManager->GenerateToken(loggedClientInfo);
        token_object["username"]  = credentials.username;
        token_object["client_id"] = client_id;
        token_object["group"]     = loggedClientInfo.group;

        callback(200, token_object.as<std::string>());
        sessionManager->setNowLoginTime(client_id.value(), loggedClientInfo.group.value());
        return client_id;
    }
    catch (const std::exception& e)
    {
        CRITICALMESSAGERESPONSE
    }
    return std::nullopt;
}

template <Client_t T, typename CALLBACK>
void ClientController<T, CALLBACK>::Logout(CALLBACK&& callback, const std::optional<std::string>& token)
{
    try
    {
        Types::LogoutData logoutData(token);
        T                 client(logoutData);
        Controller::Logout(client, callback);
    }
    catch (const std::exception& e)
    {
        CRITICALMESSAGERESPONSE
    }
}

template <Client_t T, typename CALLBACK>
void ClientController<T, CALLBACK>::Suspend(CALLBACK&& callback, const std::optional<uint64_t> client_id)
{
    try
    {
        if (!client_id.has_value())
        {
            callback(406, api::v2::JsonHelper::jsonify("Invalid id provided").as<std::string>());
            return;
        }

        Types::SuspendData suspendData(client_id.value());
        T                  client(suspendData);
        Controller::Suspend(client, callback);
    }
    catch (const std::exception& e)
    {
        CRITICALMESSAGERESPONSE
    }
}

template <Client_t T, typename CALLBACK>
void ClientController<T, CALLBACK>::Activate(CALLBACK&& callback, const std::optional<uint64_t> client_id)
{
    try
    {
        if (!client_id.has_value())
        {
            callback(406, api::v2::JsonHelper::jsonify("Invalid id provided").as<std::string>());
            return;
        }
        Types::SuspendData suspendData(client_id.value());
        T                  client(suspendData);
        Controller::Unsuspend(client, callback);
    }
    catch (const std::exception& e)
    {
        CRITICALMESSAGERESPONSE
    }
}

template <Client_t T, typename CALLBACK>
void ClientController<T, CALLBACK>::ResetPassword(CALLBACK&& callback, std::string_view data)
{
    (void)callback;
    (void)data;
}

template <Client_t T, typename CALLBACK>
void ClientController<T, CALLBACK>::GetServices(CALLBACK&& callback, std::optional<uint64_t> client_id)
{
    try
    {
        if (!client_id.has_value())
        {
            callback(400, api::v2::JsonHelper::jsonify("client_id extraction failed").as<std::string>());
            return;
        }

        T client(Types::Data_t(client_id.value()));
        Controller::GetServices(client, callback);
    }
    catch (const std::exception& e)
    {
        CRITICALMESSAGERESPONSE
    }
}
