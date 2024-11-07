
#include "utils/passwordcrypt/passwordcrypt.hpp"
#include "utils/resthelper/resthelper.hpp"

template <Client_t T>
void ClientController<T>::Create(const crow::request& req, crow::response& res, const json& request_json)
{
    (void)req;
    json response;
    try
    {
        bool                   success = false;
        typename T::ClientData client_data(request_json, res, success);

        if (success)
        {
            T client(client_data);
            if (client.exists())
            {
                RestHelper::errorResponse(res, crow::status::CONFLICT, "User already exists");
                return;
            }
            Controller::Create(res, client);
        }
        else
        {
            RestHelper::errorResponse(res, crow::status::CONFLICT, "ClientData parsing error");
            return;
        }
    }
    catch (const std::exception& e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}

template <Client_t T>
void ClientController<T>::Read(const crow::request& req, crow::response& res, const json& request_json)
{
    EntityController<T>::Read(req, res, request_json);
}

template <Client_t T>
void ClientController<T>::Update(const crow::request& req, crow::response& res, const json& request_json)
{
    (void)req;
    json response;
    try
    {
        bool                   success = false;
        typename T::ClientData client_data(request_json, res, success);

        if (success)
        {
            T client(client_data);
            Controller::Update(res, client);
        }
        else
        {
            RestHelper::errorResponse(res, crow::status::CONFLICT, "ClientData parsing error");
            return;
        }
    }
    catch (const std::exception& e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}

template <Client_t T>
void ClientController<T>::Delete(const crow::request& req, crow::response& res, const std::unordered_map<std::string, std::string>& params)
{
    EntityController<T>::Delete(req, res, params);
}

template <Client_t T>
void ClientController<T>::Search(const crow::request& req, crow::response& res, const json& request_json)
{
    EntityController<T>::Search(req, res, request_json);
}

template <Client_t T>
std::optional<uint64_t> ClientController<T>::Login(const crow::request& req, crow::response& res, const jsoncons::json& credentials)
{
    (void)req;
    json                    response;
    std::string             reponse_string;
    std::optional<uint64_t> client_id;
    try
    {
        typename T::Credentials creds;

        creds.username = credentials.at("username").as<std::string>();
        creds.password = credentials.at("password").as<std::string>();

        T client(creds);

        client_id = client.authenticate();

        if (!client_id)
        {
            RestHelper::errorResponse(res, crow::status::UNAUTHORIZED, fmt::format("User '{}' not found or wrong password", creds.username));
            return std::nullopt;
        }

        TokenManager::LoggedUserInfo loggedUserInfo;

        loggedUserInfo.userID   = client_id;
        loggedUserInfo.userName = creds.username;
        loggedUserInfo.group    = client.getGroupName();
        loggedUserInfo.llodt = sessionManager->getLastLogoutTime(loggedUserInfo.userID.value(), loggedUserInfo.group.value()).value_or("first_login");

        json token_object;
        token_object["token"]     = tokenManager->GenerateToken(loggedUserInfo);
        token_object["username"]  = creds.username;
        token_object["client_id"] = client_id;
        token_object["group"]     = loggedUserInfo.group;

        token_object.dump_pretty(reponse_string);
        RestHelper::successResponse(res, crow::status::OK, reponse_string);
        sessionManager->setNowLoginTime(client_id.value(), loggedUserInfo.group.value());
        return client_id;
    }
    catch (const std::exception& e)
    {
        RestHelper::failureResponse(res, e.what());
    }
    return std::nullopt;
}

template <Client_t T>
void ClientController<T>::Logout(const crow::request& req, crow::response& res, const std::optional<std::string>& token)
{
    (void)req;
    try
    {
        typename T::LogoutData logoutData(token);
        T                      client(logoutData);
        Controller::Logout(res, client);
    }
    catch (const std::exception& e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}

template <Client_t T>
void ClientController<T>::Suspend(const crow::request& req, crow::response& res, const std::unordered_map<std::string, std::string>& params)
{
    (void)req;
    try
    {
        auto it = params.find("id");
        if (it == params.end())
        {
            RestHelper::errorResponse(res, crow::status::NOT_ACCEPTABLE, "No id provided");
            return;
        }

        std::optional<uint64_t> client_id = std::stoull(it->second);
        if (!client_id.has_value())
        {
            RestHelper::errorResponse(res, crow::status::NOT_ACCEPTABLE, "Invalid id provided");
            return;
        }

        typename T::SuspendData suspendData(client_id.value());
        T                       client(suspendData);
        Controller::Suspend(res, client);
    }
    catch (const std::exception& e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}

template <Client_t T>
void ClientController<T>::Activate(const crow::request& req, crow::response& res, const std::unordered_map<std::string, std::string>& params)
{
    (void)req;
    try
    {
        auto it = params.find("id");
        if (it == params.end())
        {
            RestHelper::errorResponse(res, crow::status::NOT_ACCEPTABLE, "No id provided");
            return;
        }

        std::optional<uint64_t> client_id = std::stoull(it->second);
        if (!client_id.has_value())
        {
            RestHelper::errorResponse(res, crow::status::NOT_ACCEPTABLE, "Invalid id provided");
            return;
        }
        typename T::SuspendData suspendData(client_id.value());
        T                       client(suspendData);
        Controller::Unsuspend(res, client);
    }
    catch (const std::exception& e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}

template <Client_t T>
void ClientController<T>::ResetPassword(const crow::request& req, crow::response& res, const json& reset_json)
{
    (void)req;
    (void)res;
    (void)reset_json;
}

template <Client_t T>
void ClientController<T>::GetServices(const crow::request& req, crow::response& res, const std::optional<uint64_t> client_id)
{
    (void)req;
    try
    {
        if (!client_id.has_value())
        {
            RestHelper::errorResponse(res, crow::status::BAD_REQUEST, "client_id extraction failed");
            return;
        }
        T client(client_id.value());
        Controller::GetServices(res, client);
    }
    catch (const std::exception& e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}
