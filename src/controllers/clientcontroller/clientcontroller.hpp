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

template <Client_t T>
class ClientController : public EntityController<T>, public ClientControllerBase
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
    void                    Create(CALLBACK_&& callback, std::string_view data) final;
    void                    Read(CALLBACK_&& callback, std::string_view data) final;
    void                    Update(CALLBACK_&& callback, std::string_view data, std::optional<uint64_t> id) final;
    void                    Delete(CALLBACK_&& callback, std::optional<uint64_t> client_id) final;
    void                    Search(CALLBACK_&& callback, std::string_view data) final;
    std::optional<uint64_t> Login(CALLBACK_&& callback, std::string_view data) final;
    void                    Logout(CALLBACK_&& callback, const std::optional<std::string>& token) final;
    void                    Suspend(CALLBACK_&& callback, std::optional<uint64_t> client_id) final;
    void                    Activate(CALLBACK_&& callback, std::optional<uint64_t> client_id) final;
    void                    ResetPassword(CALLBACK_&& callback, std::string_view data) final;
    void                    GetServices(CALLBACK_&& callback, std::optional<uint64_t> client_id) final;

   private:
    std::shared_ptr<TokenManager>   tokenManager;
    std::shared_ptr<SessionManager> sessionManager;
};

template <Client_t T>
void ClientController<T>::Create(CALLBACK_&& callback, std::string_view data)
{
    try
    {
        bool                            success = false;
        api::v2::Global::HttpError      error;
        std::unordered_set<std::string> exclude;
        jsoncons::json                  json_data = jsoncons::json::parse(data);

        Types::CreateClient_t client_data(data, T::getTableName(), error, success);

        if (success)
        {
            T client(client_data);
            if (client.template exists<Types::CreateClient_t>())
            {
                callback(HttpStatus::CONFLICT, "User already exists");
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

template <Client_t T>
void ClientController<T>::Read(CALLBACK_&& callback, std::string_view data)
{
    EntityController<T>::Read(std::move(callback), data);
}

template <Client_t T>
void ClientController<T>::Update(CALLBACK_&& callback, std::string_view data, const std::optional<uint64_t> id)
{
    try
    {
        bool                            success = false;
        std::unordered_set<std::string> exclude{"password", "username"};
        api::v2::Global::HttpError      error;
        Types::UpdateClient_t           client_data(data, id, T::getTableName(), error, success, exclude);

        if (success)
        {
            T client(client_data);

            if (!client.template check_id_exists<Types::UpdateClient_t>())
            {
                callback(HttpStatus::BAD_REQUEST, "ID does not exist");
                return;
            }

            Controller::Update(client, std::move(callback));
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

template <Client_t T>
void ClientController<T>::Delete(CALLBACK_&& callback, const std::optional<uint64_t> client_id)
{
    EntityController<T>::Delete(std::move(callback), client_id);
}

template <Client_t T>
void ClientController<T>::Search(CALLBACK_&& callback, std::string_view data)
{
    EntityController<T>::Search(std::move(callback), data);
}

template <Client_t T>
std::optional<uint64_t> ClientController<T>::Login(CALLBACK_&& callback, std::string_view data)
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
            callback(HttpStatus::UNAUTHORIZED, fmt::format("User '{}' not found or wrong password", credentials.username));
            return std::nullopt;
        }

        TokenManager::LoggedClientInfo loggedClientInfo;

        loggedClientInfo.userID   = client_id;
        loggedClientInfo.userName = credentials.username;
        loggedClientInfo.group    = client.getGroupName();
        loggedClientInfo.llodt =
            sessionManager->getLastLogoutTime(loggedClientInfo.userID.value(), loggedClientInfo.group.value()).value_or("first_login");

        jsoncons::json token_object;
        token_object["token"]     = tokenManager->GenerateToken(loggedClientInfo);
        token_object["username"]  = credentials.username;
        token_object["client_id"] = client_id;
        token_object["group"]     = loggedClientInfo.group;

        callback(HttpStatus::OK, token_object.as<std::string>());
        sessionManager->setNowLoginTime(client_id.value(), loggedClientInfo.group.value());
        return client_id;
    }
    catch (const std::exception& e)
    {
        CRITICALMESSAGERESPONSE
    }
    return std::nullopt;
}

template <Client_t T>
void ClientController<T>::Logout(CALLBACK_&& callback, const std::optional<std::string>& token)
{
    try
    {
        Types::LogoutData logoutData(token);
        T                 client(logoutData);
        Controller::Logout(client, std::move(callback));
    }
    catch (const std::exception& e)
    {
        CRITICALMESSAGERESPONSE
    }
}

template <Client_t T>
void ClientController<T>::Suspend(CALLBACK_&& callback, const std::optional<uint64_t> client_id)
{
    try
    {
        if (!client_id.has_value())
        {
            callback(HttpStatus::Code::NOT_ACCEPTABLE, api::v2::JsonHelper::jsonify("Invalid id provided").as<std::string>());
            return;
        }

        Types::SuspendData suspendData(client_id.value());
        T                  client(suspendData);
        Controller::Suspend(client, std::move(callback));
    }
    catch (const std::exception& e)
    {
        CRITICALMESSAGERESPONSE
    }
}

template <Client_t T>
void ClientController<T>::Activate(CALLBACK_&& callback, const std::optional<uint64_t> client_id)
{
    try
    {
        if (!client_id.has_value())
        {
            callback(HttpStatus::Code::NOT_ACCEPTABLE, api::v2::JsonHelper::jsonify("Invalid id provided").as<std::string>());
            return;
        }
        Types::SuspendData suspendData(client_id.value());
        T                  client(suspendData);
        Controller::Unsuspend(client, std::move(callback));
    }
    catch (const std::exception& e)
    {
        CRITICALMESSAGERESPONSE
    }
}

template <Client_t T>
void ClientController<T>::ResetPassword(CALLBACK_&& callback, std::string_view data)
{
    (void)callback;
    (void)data;
}

template <Client_t T>
void ClientController<T>::GetServices(CALLBACK_&& callback, std::optional<uint64_t> client_id)
{
    try
    {
        if (!client_id.has_value())
        {
            callback(HttpStatus::Code::BAD_REQUEST, api::v2::JsonHelper::jsonify("client_id extraction failed").as<std::string>());
            return;
        }

        T client(Types::Data_t(client_id.value()));
        Controller::GetServices(client, std::move(callback));
    }
    catch (const std::exception& e)
    {
        CRITICALMESSAGERESPONSE
    }
}
