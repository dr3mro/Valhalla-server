#pragma once

#include <fmt/core.h>

#include <jsoncons/json.hpp>
#include <memory>
#include <optional>

#include "controllers/clientcontroller/clientcontrollerbase.hpp"
#include "controllers/entitycontroller/entitycontroller.hpp"
#include "gatekeeper/gatekeeper.hpp"
#include "utils/global/global.hpp"
#include "utils/jsonhelper/jsonhelper.hpp"

template <Client_t T>
class ClientController : public EntityController<T>, public ClientControllerBase
{
   public:
    ClientController()                = default;
    virtual ~ClientController() final = default;
    void Create(CALLBACK_&& callback, std::string_view data) final;
    void Read(CALLBACK_&& callback, std::string_view data) final;
    void Update(CALLBACK_&& callback, std::string_view data, std::optional<uint64_t> id) final;
    void Delete(CALLBACK_&& callback, std::optional<uint64_t> client_id) final;
    void Search(CALLBACK_&& callback, std::string_view data) final;
    void Login(CALLBACK_&& callback, std::string_view data, const std::string& ip_address) final;
    void Logout(CALLBACK_&& callback, const std::optional<std::string>& token, const std::string& ip_address) final;
    void Suspend(CALLBACK_&& callback, std::optional<uint64_t> client_id) final;
    void Activate(CALLBACK_&& callback, std::optional<uint64_t> client_id) final;
    void ResetPassword(CALLBACK_&& callback, std::string_view data) final;
    void GetServices(CALLBACK_&& callback, std::optional<uint64_t> client_id) final;

   private:
    std::shared_ptr<GateKeeper> gateKeeper = Store::getObject<GateKeeper>();
};

template <Client_t T>
void ClientController<T>::Create(CALLBACK_&& callback, std::string_view data)
{
    try
    {
        bool                            success = false;
        api::v2::Http::Error            error;
        std::unordered_set<std::string> exclude;
        jsoncons::json                  json_data = jsoncons::json::parse(data);

        Types::CreateClient_t client_data(data, T::getTableName(), error, success);

        if (success)
        {
            T client(client_data);
            if (client.template exists<Types::CreateClient_t>())
            {
                callback(api::v2::Http::Status::CONFLICT, "User already exists");
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
        bool            success = false;
        Validator::Rule rule(Validator::Rule::Action::ASSERT_IMMUTABLE, {"username", "password"});

        api::v2::Http::Error  error;
        Types::UpdateClient_t client_data(data, id, T::getTableName(), error, success, rule);

        if (success)
        {
            T client(client_data);
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
    gateKeeper->removeSession(client_id, T::getTableName());
}

template <Client_t T>
void ClientController<T>::Search(CALLBACK_&& callback, std::string_view data)
{
    EntityController<T>::Search(std::move(callback), data);
}

template <Client_t T>
void ClientController<T>::Login(CALLBACK_&& callback, std::string_view data, const std::string& ip_address)
{
    gateKeeper->login(std::move(callback), data, ip_address, T::getTableName());
}

template <Client_t T>
void ClientController<T>::Logout(CALLBACK_&& callback, const std::optional<std::string>& token, const std::string& ip_address)
{
    gateKeeper->logout(std::move(callback), token, ip_address, T::getTableName());
}

template <Client_t T>
void ClientController<T>::Suspend(CALLBACK_&& callback, const std::optional<uint64_t> client_id)
{
    try
    {
        if (!client_id.has_value())
        {
            callback(api::v2::Http::Status::NOT_ACCEPTABLE, api::v2::JsonHelper::jsonify("Invalid id provided").as<std::string>());
            return;
        }

        Types::SuspendData suspendData(client_id.value());
        T                  client(suspendData);
        Controller::Suspend(client, std::move(callback));
        gateKeeper->removeSession(client_id, T::getTableName());
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
            callback(api::v2::Http::Status::NOT_ACCEPTABLE, api::v2::JsonHelper::jsonify("Invalid id provided").as<std::string>());
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
            callback(api::v2::Http::Status::BAD_REQUEST, api::v2::JsonHelper::jsonify("client_id extraction failed").as<std::string>());
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
