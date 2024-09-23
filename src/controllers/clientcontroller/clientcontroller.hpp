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

#include "controllers/base/controller/controller.hpp"
#include "controllers/clientcontroller/clientcontrollerbase.hpp"
#include "controllers/databasecontroller/databasecontroller.hpp"
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
 */

class ClientController : public ClientControllerBase, public Controller
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
    void                    CreateClient(const crow::request& req, crow::response& res, const json& criteria) override;
    std::optional<uint64_t> AuthenticateClient(const crow::request& req, crow::response& res, const jsoncons::json& credentials) override;
    void                    ReadClient(const crow::request& req, crow::response& res, const json& criteria) override;
    void                    UpdateClient(const crow::request& req, crow::response& res, const json& criteria) override;
    void                    DeleteClient(const crow::request& req, crow::response& res, const json& delete_json) override;
    void                    SearchClient(const crow::request& req, crow::response& res, const json& search_json) override;
    void                    LogoutClient(const crow::request& req, crow::response& res, const std::optional<std::string>& token) override;

   protected:
    std::shared_ptr<TokenManager>   tokenManager;
    std::shared_ptr<PasswordCrypt>  passwordCrypt;
    std::shared_ptr<SessionManager> sessionManager;
};

template <Client_t T>
/**
 * @brief Creates a new client user.
 *
 * This function is responsible for creating a new client user. It takes the
 * request body as a JSON object, parses it into the appropriate user data type,
 * validates the user data, and then creates the new user.
 *
 * @param req The Crow request object containing the user data.
 * @param res The Crow response object to send the result of the operation.
 */
void ClientController<T>::CreateClient(const crow::request& req, crow::response& res, const json& criteria)
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
            Create<T>(res, client);
        }
    }
    catch (const std::exception& e)
    {
        RestHelper::failureResponse(std::ref(res), e.what());
    }
}

template <Client_t T>
/**
 * @brief Authenticates a user and generates a token.
 *
 * This function is responsible for authenticating a user by verifying their
 * credentials, and if successful, generating an authentication token that can
 * be used for subsequent requests.
 *
 * @param res The Crow response object to send the result of the operation.
 * @param credentials A JSON object containing the user's username and password.
 * @return std::optional<uint64_t> The user ID of the authenticated user, or
 * std::nullopt if authentication failed.
 */
std::optional<uint64_t> ClientController<T>::AuthenticateClient(const crow::request& req, crow::response& res, const jsoncons::json& credentials)
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
        RestHelper::failureResponse(std::ref(res), e.what());
    }
    return std::nullopt;
}

template <Client_t T>
/**
 * @brief Reads a client entity based on the provided criteria.
 *
 * This function is responsible for reading a client entity from the underlying
 * data source based on the provided criteria. The criteria includes the client
 * ID and a list of schema fields to include in the response.
 *
 * @param res The Crow response object to send the result of the operation.
 * @param criteria A JSON object containing the client ID and the list of schema
 * fields to include.
 */
void ClientController<T>::ReadClient(const crow::request& req, crow::response& res, const json& criteria)
{
    (void)req;
    try
    {
        uint64_t                 id     = criteria.at("id").as<uint64_t>();
        std::vector<std::string> schema = criteria.at("data").as<std::vector<std::string>>();

        Entity::ReadData readData(schema, id);
        T                client(readData);
        Controller::Read(std::ref(res), client);
    }
    catch (const std::exception& e)
    {
        RestHelper::failureResponse(std::ref(res), e.what());
    }
}

template <Client_t T>
/**
 * @brief Updates a client entity based on the provided data.
 *
 * This function is responsible for updating a client entity in the underlying
 * data source based on the provided data. The data includes the client ID and a
 * JSON payload containing the updated fields.
 *
 * @param req The Crow request object containing the update data.
 * @param res The Crow response object to send the result of the operation.
 */
void ClientController<T>::UpdateClient(const crow::request& req, crow::response& res, const json& criteria)
{
    (void)req;
    try
    {
        bool       success = false;
        ClientData client_data(criteria, res, success);
        if (success)
        {
            T client(client_data);
            Controller::Update(std::ref(res), client);
        }
    }
    catch (const std::exception& e)
    {
        RestHelper::failureResponse(std::ref(res), e.what());
    }
}

template <Client_t T>
/**
 * @brief Deletes a client entity based on the provided data.
 *
 * This function is responsible for deleting a client entity from the underlying
 * data source based on the provided data. The data includes the client ID.
 *
 * @param req The Crow request object containing the delete data.
 * @param res The Crow response object to send the result of the operation.
 * @param delete_json A JSON object containing the client ID and other data
 * required for the delete operation.
 */
void ClientController<T>::DeleteClient(const crow::request& req, crow::response& res, const json& delete_json)
{
    (void)req;
    try
    {
        Entity::DeleteData deleteData(delete_json);
        T                  client(deleteData);
        Controller::Delete(std::ref(res), client);
    }
    catch (const std::exception& e)
    {
        RestHelper::failureResponse(std::ref(res), e.what());
    }
}

template <Client_t T>
/**
 * @brief Searches for a client entity based on the provided data.
 *
 * This function is responsible for searching for a client entity in the
 * underlying data source based on the provided search data. The search data is
 * provided in a JSON object.
 *
 * @param req The Crow request object containing the search data.
 * @param res The Crow response object to send the result of the search
 * operation.
 * @param search_json A JSON object containing the search criteria.
 */
void ClientController<T>::SearchClient(const crow::request& req, crow::response& res, const json& search_json)
{
    (void)req;
    try
    {
        bool                   success = false;
        typename T::SearchData searchData(search_json, success);
        if (success)
        {
            T client(searchData);
            Controller::Search(std::ref(res), client);
        }
    }
    catch (const std::exception& e)
    {
        RestHelper::failureResponse(std::ref(res), e.what());
    }
}

template <Client_t T>
/**
 * @brief Logs out a client by invalidating the provided token.
 *
 * This function is responsible for logging out a client by invalidating the
 * provided token. It uses the `Entity::LogoutData` class to encapsulate the
 * token data and the `Controller::Logout` function to perform the logout
 * operation.
 *
 * @param res The Crow response object to send the result of the logout
 * operation.
 * @param token The optional token to be invalidated.
 */
void ClientController<T>::LogoutClient(const crow::request& req, crow::response& res, const std::optional<std::string>& token)
{
    (void)req;
    try
    {
        Entity::LogoutData logoutData(token);
        T                  client(logoutData);
        Controller::Logout(std::ref(res), client);
    }
    catch (const std::exception& e)
    {
        RestHelper::failureResponse(std::ref(res), e.what());
    }
}
