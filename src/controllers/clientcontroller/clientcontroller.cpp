#include "controllers/clientcontroller/clientcontroller.hpp"

#include <fmt/core.h>

#include <cstdint>
#include <exception>
#include <jsoncons/basic_json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "controllers/base/controller/controller.hpp"
#include "controllers/entitycontroller/entitycontroller.hpp"
#include "controllers/entitycontroller/entitycontrollerbase.hpp"
#include "entities/base/types.hpp"
#include "entities/people/provider.hpp"
#include "entities/people/user.hpp"
#include "utils/global/callback.hpp"
#include "utils/global/concepts.hpp"
#include "utils/global/http.hpp"
#include "utils/jsonhelper/jsonhelper.hpp"
#include "validator/validator.hpp"

using HttpError      = api::v2::Http::Error;
using CreateClient_t = api::v2::Types::CreateClient_t;
using UpdateClient_t = api::v2::Types::UpdateClient_t;
using SuspendData    = api::v2::Types::SuspendData;
using Data_t         = api::v2::Types::Data_t;

template <Client_t T>
void ClientController<T>::Create(CALLBACK_&& callback, [[maybe_unused]] const Requester&& requester, std::string_view data)
{
    try
    {
        bool           success = false;
        HttpError      error;
        jsoncons::json json_data = jsoncons::json::parse(data);

        CreateClient_t client_data(data, T::getTableName(), error, success);

        if (success)
        {
            T client(client_data);
            if (client.template exists<CreateClient_t>())
            {
                callback(api::v2::Http::Status::CONFLICT, "Client already exists");
                return;
            }

            if (!gateKeeper->canCreate<T>(requester, json_data, error))
            {
                callback(error.code, error.message);
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
void ClientController<T>::Read(CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
    EntityController<T>::Read(std::move(callback), std::move(requester), data);
}

template <Client_t T>
void ClientController<T>::Update(CALLBACK_&& callback, const Requester&& requester, std::string_view data, const std::optional<uint64_t> _id)
{
    try
    {
        bool            success = false;
        Validator::Rule rule(Validator::Rule::Action::ASSERT_IMMUTABLE, {"username", "password"});

        HttpError      error;
        UpdateClient_t client_data(data, _id, T::getTableName(), error, success, rule);

        if (success)
        {
            T client(client_data);
            if (!gateKeeper->canUpdate<T>(requester, _id.value(), error))
            {
                callback(error.code, error.message);
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
void ClientController<T>::Delete(CALLBACK_&& callback, const Requester&& requester, const std::optional<uint64_t> client_id)
{
    EntityController<T>::Delete(std::move(callback), std::move(requester), client_id);
    gateKeeper->removeSession(client_id, T::getTableName());
}

template <Client_t T>
void ClientController<T>::Search(CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
    EntityController<T>::Search(std::move(callback), std::move(requester), data);
}

template <Client_t T>
void ClientController<T>::Login(CALLBACK_&& callback, [[maybe_unused]] const Requester&& requester, std::string_view data, const std::string& ip_address)
{
    gateKeeper->login(std::move(callback), data, ip_address, T::getTableName());
}

template <Client_t T>
void ClientController<T>::Logout(
    CALLBACK_&& callback, [[maybe_unused]] const Requester&& requester, const std::optional<std::string>& token, const std::string& ip_address)
{
    gateKeeper->logout(std::move(callback), token, ip_address, T::getTableName());
}

template <Client_t T>
void ClientController<T>::Suspend(CALLBACK_&& callback, const Requester&& requester, const std::optional<uint64_t> client_id)
{
    try
    {
        if (!client_id.has_value())
        {
            callback(api::v2::Http::Status::NOT_ACCEPTABLE, "Invalid id provided");
            return;
        }

        SuspendData suspendData(client_id.value());
        T           client(suspendData);
        HttpError   error;

        if (!gateKeeper->canToggleActive<T>(requester, client_id.value(), error))
        {
            callback(error.code, error.message);
            return;
        }

        Controller::Suspend(client, std::move(callback));
        gateKeeper->removeSession(client_id, T::getTableName());
    }
    catch (const std::exception& e)
    {
        CRITICALMESSAGERESPONSE
    }
}

template <Client_t T>
void ClientController<T>::Activate(CALLBACK_&& callback, const Requester&& requester, const std::optional<uint64_t> client_id)
{
    try
    {
        if (!client_id.has_value())
        {
            callback(api::v2::Http::Status::NOT_ACCEPTABLE, api::v2::JsonHelper::jsonify("Invalid id provided").as<std::string>());
            return;
        }
        SuspendData suspendData(client_id.value());

        T client(suspendData);

        HttpError error;
        if (!gateKeeper->canToggleActive<T>(requester, client_id.value(), error))
        {
            callback(error.code, error.message);
            return;
        }

        Controller::Unsuspend(client, std::move(callback));
    }
    catch (const std::exception& e)
    {
        CRITICALMESSAGERESPONSE
    }
}

template <Client_t T>
void ClientController<T>::ResetPassword(CALLBACK_&& callback, [[maybe_unused]] const Requester&& requester, std::string_view data)
{
    (void)std::move(callback);
    (void)data;
}

template <Client_t T>
void ClientController<T>::GetServices(CALLBACK_&& callback, const Requester&& requester, std::optional<uint64_t> client_id)
{
    try
    {
        if (!client_id.has_value())
        {
            callback(api::v2::Http::Status::BAD_REQUEST, api::v2::JsonHelper::jsonify("client_id extraction failed").as<std::string>());
            return;
        }

        T client(Data_t(client_id.value()));

        HttpError error;
        if (!gateKeeper->canGetServices<T>(requester, client_id.value(), error))
        {
            callback(error.code, error.message);
            return;
        }

        Controller::GetServices(client, std::move(callback));
    }
    catch (const std::exception& e)
    {
        CRITICALMESSAGERESPONSE
    }
}

#define INSTANTIATE_CLIENT_CONTROLLER(TYPE) /* NOLINT(cppcoreguidelines-macro-usage)*/                                                                    \
    template void ClientController<TYPE>::Create(CALLBACK_&& callback, const Requester&& requester, std::string_view data);                               \
    template void ClientController<TYPE>::Read(CALLBACK_&& callback, const Requester&& requester, std::string_view data);                                 \
    template void ClientController<TYPE>::Update(CALLBACK_&& callback, const Requester&& requester, std::string_view data, std::optional<uint64_t> id);   \
    template void ClientController<TYPE>::Delete(CALLBACK_&& callback, const Requester&& requester, std::optional<uint64_t> client_id);                   \
    template void ClientController<TYPE>::Search(CALLBACK_&& callback, const Requester&& requester, std::string_view data);                               \
    template void ClientController<TYPE>::Login(CALLBACK_&& callback, const Requester&& requester, std::string_view data, const std::string& ip_address); \
    template void ClientController<TYPE>::Logout(                                                                                                         \
        CALLBACK_&& callback, const Requester&& requester, const std::optional<std::string>& token, const std::string& ip_address);                       \
    template void ClientController<TYPE>::Suspend(CALLBACK_&& callback, const Requester&& requester, std::optional<uint64_t> client_id);                  \
    template void ClientController<TYPE>::Activate(CALLBACK_&& callback, const Requester&& requester, std::optional<uint64_t> client_id);                 \
    template void ClientController<TYPE>::ResetPassword(CALLBACK_&& callback, const Requester&& requester, std::string_view data);                        \
    template void ClientController<TYPE>::GetServices(CALLBACK_&& callback, const Requester&& requester, std::optional<uint64_t> client_id);

INSTANTIATE_CLIENT_CONTROLLER(User)
INSTANTIATE_CLIENT_CONTROLLER(Provider)
