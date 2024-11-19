
#include "entities/base/types.hpp"
#include "utils/helper/helper.hpp"  // remove warnings
#include "utils/passwordcrypt/passwordcrypt.hpp"

template <Client_t T>
void ClientController<T>::Create(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data)
{
    try
    {
        bool              success = false;
        Types::ClientData client_data(data, callback, success);

        if (success)
        {
            T client(client_data);
            if (client.exists())
            {
                Helper::errorResponse(drogon::k409Conflict, "User already exists", callback);
                return;
            }
            Controller::Create(client, callback);
        }
    }
    catch (const std::exception& e)
    {
        Helper::failureResponse(e.what(), callback);
    }
}

template <Client_t T>
void ClientController<T>::Read(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data)
{
    EntityController<T>::Read(std::move(callback), data);
}

template <Client_t T>
void ClientController<T>::Update(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data)
{
    jsoncons::json json_data;
    try
    {
        json_data                 = jsoncons::json::parse(data);
        bool              success = false;
        Types::ClientData client_data(data, callback, success);

        if (success)
        {
            T client(client_data);
            Controller::Update(client, callback);
        }
        else
        {
            Helper::errorResponse(drogon::k409Conflict, "ClientData parsing error", callback);
            return;
        }
    }
    catch (const std::exception& e)
    {
        Helper::failureResponse(e.what(), callback);
    }
}

template <Client_t T>
void ClientController<T>::Delete(std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                 const std::unordered_map<std::string, std::string>&   params)
{
    EntityController<T>::Delete(std::move(callback), params);
}

template <Client_t T>
void ClientController<T>::Search(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data)
{
    EntityController<T>::Search(std::move(callback), data);
}

template <Client_t T>
std::optional<uint64_t> ClientController<T>::Login(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data)
{
    json                    response;
    jsoncons::json          credentials_j;
    std::optional<uint64_t> client_id;
    Types::Credentials      credentials;
    try
    {
        credentials_j        = jsoncons::json::parse(data);
        credentials.username = credentials_j.at("username").as<std::string>();
        credentials.password = credentials_j.at("password").as<std::string>();

        T client(credentials);

        client_id = client.authenticate();

        if (!client_id)
        {
            Helper::errorResponse(drogon::k401Unauthorized, fmt::format("User '{}' not found or wrong password", credentials.username), callback);
            return std::nullopt;
        }

        TokenManager::LoggedUserInfo loggedUserInfo;

        loggedUserInfo.userID   = client_id;
        loggedUserInfo.userName = credentials.username;
        loggedUserInfo.group    = client.getGroupName();
        loggedUserInfo.llodt = sessionManager->getLastLogoutTime(loggedUserInfo.userID.value(), loggedUserInfo.group.value()).value_or("first_login");

        json token_object;
        token_object["token"]     = tokenManager->GenerateToken(loggedUserInfo);
        token_object["username"]  = credentials.username;
        token_object["client_id"] = client_id;
        token_object["group"]     = loggedUserInfo.group;

        Helper::successResponse(Helper::stringify(token_object), callback);
        sessionManager->setNowLoginTime(client_id.value(), loggedUserInfo.group.value());
        return client_id;
    }
    catch (const std::exception& e)
    {
        Helper::failureResponse(e.what(), callback);
    }
    return std::nullopt;
}

template <Client_t T>
void ClientController<T>::Logout(std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::optional<std::string>& token)
{
    try
    {
        Types::LogoutData logoutData(token);
        T                 client(logoutData);
        Controller::Logout(client, callback);
    }
    catch (const std::exception& e)
    {
        Helper::failureResponse(e.what(), callback);
    }
}

template <Client_t T>
void ClientController<T>::Suspend(std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                  const std::unordered_map<std::string, std::string>&   params)
{
    try
    {
        auto it = params.find("id");
        if (it == params.end())
        {
            Helper::errorResponse(drogon::k406NotAcceptable, "No id provided", callback);
            return;
        }

        std::optional<uint64_t> client_id = std::stoull(it->second);
        if (!client_id.has_value())
        {
            Helper::errorResponse(drogon::k406NotAcceptable, "Invalid id provided", callback);
            return;
        }

        Types::SuspendData suspendData(client_id.value());
        T                  client(suspendData);
        Controller::Suspend(client, callback);
    }
    catch (const std::exception& e)
    {
        Helper::failureResponse(e.what(), callback);
    }
}

template <Client_t T>
void ClientController<T>::Activate(std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                   const std::unordered_map<std::string, std::string>&   params)
{
    try
    {
        auto it = params.find("id");
        if (it == params.end())
        {
            Helper::errorResponse(drogon::k406NotAcceptable, "No id provided", callback);
            return;
        }

        std::optional<uint64_t> client_id = std::stoull(it->second);
        if (!client_id.has_value())
        {
            Helper::errorResponse(drogon::k406NotAcceptable, "Invalid id provided", callback);
            return;
        }
        Types::SuspendData suspendData(client_id.value());
        T                  client(suspendData);
        Controller::Unsuspend(client, callback);
    }
    catch (const std::exception& e)
    {
        Helper::failureResponse(e.what(), callback);
    }
}

template <Client_t T>
void ClientController<T>::ResetPassword(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data)
{
    (void)callback;
    (void)data;
}

template <Client_t T>
void ClientController<T>::GetServices(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::optional<uint64_t> client_id)
{
    try
    {
        if (!client_id.has_value())
        {
            Helper::errorResponse(drogon::k400BadRequest, "client_id extraction failed", callback);
            return;
        }

        T client(Types::Data_t(client_id.value()));
        Controller::GetServices(client, callback);
    }
    catch (const std::exception& e)
    {
        Helper::failureResponse(e.what(), callback);
    }
}
