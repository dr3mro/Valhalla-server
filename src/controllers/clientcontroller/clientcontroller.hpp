/**
 * @brief The ClientController class is responsible for handling client-related
 * operations, such as creating, authenticating, reading, updating, deleting,
 * and searching clients.
 *
 * The ClientController class is a template class that takes a type parameter
 * `T`, which is expected to be a client entity class that inherits from the
 * `Entity` class. The class provides various member functions to perform CRUD
 * (Create, Read, Update, Delete) operations on clients, as well as
 * authentication and logout functionality.
 *
 * The constructor of the ClientController class initializes the necessary
 * dependencies, such as the TokenManager, PasswordCrypt, and SessionManager, by
 * retrieving them from the global Store.
 *
 * The public member functions of the ClientController class are:
 *
 * - `CreateUser`: Creates a new client user based on the provided data in the
 * request body.
 * - `AuthenticateUser`: Authenticates a client user based on the provided
 * credentials and returns the client ID if successful.
 * - `ReadClient`: Reads a client based on the provided criteria (e.g., ID and
 * schema).
 * - `UpdateClient`: Updates a client based on the provided payload in the
 * request body.
 * - `DeleteClient`: Deletes a client based on the provided payload in the
 * request body.
 * - `SearchClient`: Searches for clients based on the provided search criteria.
 * - `LogoutClient`: Logs out a client based on the provided token.
 *
 * The protected member variables of the ClientController class are:
 *
 * - `tokenManager`: A shared pointer to the TokenManager instance.
 * - `passwordCrypt`: A shared pointer to the PasswordCrypt instance.
 * - `sessionManager`: A shared pointer to the SessionManager instance.
 */
// clientcontroller.hpp

#pragma once

#include <crow.h>
#include <fmt/core.h>

#include <jsoncons/json.hpp>
#include <memory>
#include <type_traits>

#include "controllers/clientcontroller/clientcontrollerbase.hpp"
#include "controllers/databasecontroller/databasecontroller.hpp"
#include "controllers/entitycontroller/entitycontroller.hpp"
#include "entities/base/client.hpp"
#include "entities/base/datatypes/clientdata.hpp"
#include "utils/passwordcrypt/passwordcrypt.hpp"
#include "utils/resthelper/resthelper.hpp"
#include "utils/sessionmanager/sessionmanager.hpp"
#include "utils/tokenmanager/tokenmanager.hpp"

using json = jsoncons::json;

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
            passwordCrypt  = Store::getObject<PasswordCrypt>();
            sessionManager = Store::getObject<SessionManager>();
        }
        catch (const std::exception& e)
        {
            std::cerr << fmt::format("Exception in ClientController constructor: {}\n", e.what());
            exit(EXIT_FAILURE);
        }
    }

    ~ClientController() override = default;

    // PUBLIC
    void                    Create(const crow::request& req, crow::response& res, const json& criteria) final;
    std::optional<uint64_t> Authenticate(const crow::request& req, crow::response& res, const jsoncons::json& credentials) final;
    void                    Read(const crow::request& req, crow::response& res, const json& criteria) final;
    void                    Update(const crow::request& req, crow::response& res, const json& criteria) final;
    void                    Delete(const crow::request& req, crow::response& res, const json& delete_json) final;
    void                    Search(const crow::request& req, crow::response& res, const json& search_json) final;
    void                    Logout(const crow::request& req, crow::response& res, const std::optional<std::string>& token) final;
    void                    Suspend(const crow::request& req, crow::response& res, const json& criteria) final;
    void                    Activate(const crow::request& req, crow::response& res, const json& criteria) final;
    void                    ResetPassword(const crow::request& req, crow::response& res, const json& reset_json) final;
    void                    GetServices(const crow::request& req, crow::response& res, std::optional<uint64_t> client_id) final;

   protected:
    std::shared_ptr<TokenManager>   tokenManager;
    std::shared_ptr<PasswordCrypt>  passwordCrypt;
    std::shared_ptr<SessionManager> sessionManager;
};

template <Client_t T>

void ClientController<T>::Create(const crow::request& req, crow::response& res, const json& criteria)
{
    (void)req;
    json response;
    try
    {
        bool       success = false;
        ClientData client_data(criteria, res, success);

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
    }
    catch (const std::exception& e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}

template <Client_t T>

std::optional<uint64_t> ClientController<T>::Authenticate(const crow::request& req, crow::response& res, const jsoncons::json& credentials)
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
        token_object["token"]    = tokenManager->GenerateToken(loggedUserInfo);
        token_object["username"] = creds.username;
        token_object["user_id"]  = client_id;
        token_object["group"]    = loggedUserInfo.group;

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

void ClientController<T>::Read(const crow::request& req, crow::response& res, const json& criteria)
{
    (void)req;
    try
    {
        uint64_t                 id     = criteria.at("id").as<uint64_t>();
        std::vector<std::string> schema = criteria.at("data").as<std::vector<std::string>>();

        Entity::ReadData readData(schema, id);
        T                client(readData);
        Controller::Read(res, client);
    }
    catch (const std::exception& e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}

template <Client_t T>

void ClientController<T>::Update(const crow::request& req, crow::response& res, const json& criteria)
{
    (void)req;
    try
    {
        bool       success = false;
        ClientData client_data(criteria, res, success);
        if (success)
        {
            T client(client_data);
            Controller::Update(res, client);
        }
    }
    catch (const std::exception& e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}

template <Client_t T>

void ClientController<T>::Delete(const crow::request& req, crow::response& res, const json& delete_json)
{
    (void)req;
    try
    {
        Entity::DeleteData deleteData(delete_json);
        T                  client(deleteData);
        Controller::Delete(res, client);
    }
    catch (const std::exception& e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}

template <Client_t T>

void ClientController<T>::Search(const crow::request& req, crow::response& res, const json& search_json)
{
    (void)req;
    try
    {
        bool                   success = false;
        typename T::SearchData searchData(search_json, success);
        if (success)
        {
            T client(searchData);
            Controller::Search(res, client);
        }
    }
    catch (const std::exception& e)
    {
        RestHelper::failureResponse(res, e.what());
    }
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
void ClientController<T>::Suspend(const crow::request& req, crow::response& res, const json& criteria)
{
    (void)req;
    try
    {
        uint64_t                client_id = criteria.at("id").as<uint64_t>();
        typename T::SuspendData suspendData(client_id);
        T                       client(suspendData);
        Controller::Suspend(res, client);
    }
    catch (const std::exception& e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}

template <Client_t T>
void ClientController<T>::Activate(const crow::request& req, crow::response& res, const json& criteria)
{
    (void)req;
    try
    {
        uint64_t                client_id = criteria.at("id").as<uint64_t>();
        typename T::SuspendData suspendData(client_id);
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