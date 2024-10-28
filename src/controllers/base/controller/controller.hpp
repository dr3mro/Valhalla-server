/**
 * @class Controller
 * @brief Base class for handling CRUD (Create, Read, Update, Delete) and search
 * operations for entities.
 *
 * The `Controller` class provides a set of template methods for performing CRUD
 * and search operations on entities. It uses the `DatabaseController`,
 * `SessionManager`, and `TokenManager` classes to interact with the database
 * and manage user sessions and tokens.
 *
 * The `Create`, `Read`, `Update`, `Delete`, and `Search` methods are templates
 * that take an entity type `T` as a parameter. These methods use the SQL
 * statements provided by the entity type to execute the corresponding CRUD or
 * search operation.
 *
 * The `Logout` method is used to log out a user by validating the provided
 * token and updating the session information.
 *
 * The `Controller` class is designed to be a base class for more specialized
 * controllers that handle specific entity types.
 */

#pragma once
#include <crow.h>
#include <fmt/core.h>  // Include fmt library for string formatting
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <jsoncons/json.hpp>

#include "controllers/databasecontroller/databasecontroller.hpp"
#include "entities/base/entity.hpp"
#include "utils/resthelper/resthelper.hpp"
#include "utils/sessionmanager/sessionmanager.hpp"
#include "utils/tokenmanager/tokenmanager.hpp"
using json = jsoncons::json;

class Controller
{
   public:
    /**
     * @brief Constructs a new `Controller` object.
     *
     * The `Controller` class is the base class for handling CRUD (Create, Read,
     * Update, Delete) and search operations for entities. This constructor
     * initializes the necessary dependencies, such as the `DatabaseController`,
     * `SessionManager`, and `TokenManager` objects, which are used by the CRUD
     * and search operations.
     *
     * If any of the dependencies cannot be retrieved from the `Store`, an
     * exception is thrown and the program exits with a failure status.
     */
    Controller()
    {
        try
        {
            databaseController = Store::getObject<DatabaseController>();
            sessionManager     = Store::getObject<SessionManager>();
            tokenManager       = Store::getObject<TokenManager>();
        }
        catch (const std::exception &e)
        {
            std::cerr << fmt::format("Exception in Controller constructor: {}\n", e.what());
            exit(EXIT_FAILURE);
        }
    }
    virtual ~Controller() = default;

    // CRUDS
    template <typename T>
    /**
     * @brief Creates a new entity of type T in the database.
     *
     * This method uses the SQL create statement provided by the entity type T to
     * insert a new record into the database. The `cruds` helper function is
     * called with the appropriate parameters to execute the SQL statement and
     * handle the response.
     *
     * @param res The Crow response object to send the result of the create
     * operation.
     * @param entity The entity object of type T to be created.
     */
    void Create(crow::response &res, T &entity)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlCreateStatement;
        cruds(res, entity, sqlstatement, dbexec);
    }

    template <typename T>
    /**
     * @brief Reads an entity of type T from the database.
     *
     * This method uses the SQL read statement provided by the entity type T to
     * retrieve a record from the database. The `cruds` helper function is called
     * with the appropriate parameters to execute the SQL statement and handle the
     * response.
     *
     * @param res The Crow response object to send the result of the read
     * operation.
     * @param entity The entity object of type T to be read.
     */
    void Read(crow::response &res, T &entity)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlReadStatement;
        cruds(res, entity, sqlstatement, dbrexec);
    }
    template <typename T>
    /**
     * @brief Updates an entity of type T in the database.
     *
     * This method uses the SQL update statement provided by the entity type T to
     * modify an existing record in the database. The `cruds` helper function is
     * called with the appropriate parameters to execute the SQL statement and
     * handle the response.
     *
     * @param res The Crow response object to send the result of the update
     * operation.
     * @param entity The entity object of type T to be updated.
     */
    void Update(crow::response &res, T &entity)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlUpdateStatement;
        cruds(res, entity, sqlstatement, dbexec);
    }
    template <typename T>
    /**
     * @brief Deletes an entity of type T from the database.
     *
     * This method uses the SQL delete statement provided by the entity type T to
     * remove a record from the database. The `cruds` helper function is called
     * with the appropriate parameters to execute the SQL statement and handle the
     * response.
     *
     * @param res The Crow response object to send the result of the delete
     * operation.
     * @param entity The entity object of type T to be deleted.
     */
    void Delete(crow::response &res, T &entity)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlDeleteStatement;
        cruds(res, entity, sqlstatement, dbexec);
    }
    template <typename T>
    /**
     * @brief Searches for entities of type T in the database.
     *
     * This method uses the SQL search statement provided by the entity type T to
     * retrieve records from the database that match the search criteria. The
     * `databaseController` is used to execute the SQL statement and handle the
     * response.
     *
     * The method will return a JSON object with the search results. If the number
     * of results exceeds the limit specified in the `Entity::SearchData` object,
     * the JSON object will include a `"more"` flag set to `true` and an
     * `"offset"` value to be used for pagination.
     *
     * @param res The Crow response object to send the result of the search
     * operation.
     * @param entity The entity object of type T containing the search criteria.
     */
    void Search(crow::response &res, T &entity)
    {
        json                       response_json;
        json                       query_results_json;
        std::string                results;
        std::optional<std::string> query;

        try
        {
            query = entity.getSqlSearchStatement();

            if (query)
            {
                query_results_json   = databaseController->executeSearchQuery(query.value());
                size_t results_count = query_results_json.size();

                if (results_count > std::any_cast<typename T::SearchData>(entity.getData()).limit)
                {
                    response_json["more"]   = true;
                    response_json["offset"] = std::any_cast<typename T::SearchData>(entity.getData()).offset +
                                              std::any_cast<typename T::SearchData>(entity.getData()).limit;
                    query_results_json.erase(query_results_json.array_range().end() - 1);
                }
                else
                {
                    response_json["more"]   = false;
                    response_json["offset"] = 0;
                }
            }

            if (query_results_json.empty())
            {
                response_json["results"] = jsoncons::json();
                response_json.dump_pretty(results);
                RestHelper::successResponse(res, crow::status::OK, results);
            }
            else
            {
                response_json["results"] = query_results_json;
                response_json.dump_pretty(results);
                RestHelper::successResponse(res, crow::status::OK, results);
            }
        }
        catch (const std::exception &e)
        {
            response_json["results"] = jsoncons::json();
            response_json["error"]   = e.what();
            response_json.dump_pretty(results);
            RestHelper::failureResponse(res, results);
        }
    }
    template <typename T>
    /**
     * @brief Logs out the user by validating the token, setting the logout time,
     * and returning a success or failure response.
     *
     * This method is responsible for handling the logout process for a user. It
     * first validates the token provided in the `Entity::LogoutData` object. If
     * the token is valid, it sets the logout time for the user's session using
     * the `sessionManager`. Finally, it sends a response back to the client
     * indicating whether the logout was successful or not.
     *
     * @param res The Crow response object to send the result of the logout
     * operation.
     * @param entity The entity object of type T containing the logout data,
     * including the token.
     */
    void Logout(crow::response &res, T &entity)
    {
        TokenManager::LoggedUserInfo loggedUserInfo;

        try
        {
            loggedUserInfo.token = std::any_cast<Entity::LogoutData>(entity.getData()).token;
            loggedUserInfo.group = entity.getGroupName();

            bool status = tokenManager->ValidateToken(loggedUserInfo);
            if (!status)
            {
                RestHelper::errorResponse(res, crow::status::UNAUTHORIZED, "Logout failure.");
                return;
            }
            sessionManager->setNowLogoutTime(loggedUserInfo.userID.value(), loggedUserInfo.group.value());
            RestHelper::successResponse(res, crow::status::OK, "Logout success");
        }
        catch (const std::exception &e)
        {
            RestHelper::failureResponse(res, e.what());
        }
    }

    template <typename T>
    void Suspend(crow::response &res, T &entity)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlSuspendStatement;
        cruds(res, entity, sqlstatement, dbexec);
    }

    template <typename T>
    void Unsuspend(crow::response &res, T &entity)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlActivateStatement;
        cruds(res, entity, sqlstatement, dbexec);
    }

   protected:
    /**
     * @brief Shared pointers to the DatabaseController, SessionManager, and
     * TokenManager instances.
     *
     * These shared pointers provide access to the database controller, session
     * manager, and token manager components used throughout the application.
     */
    std::shared_ptr<DatabaseController> databaseController;
    std::shared_ptr<SessionManager>     sessionManager;
    std::shared_ptr<TokenManager>       tokenManager;

    /**
     * @brief Function pointers to the DatabaseController's executeQuery and
     * executeReadQuery methods.
     *
     * These function pointers allow for easy invocation of the
     * DatabaseController's query execution methods.
     */
    std::optional<json> (DatabaseController::*dbexec)(const std::string &)         = &DatabaseController::executeQuery;
    std::optional<json> (DatabaseController::*dbrexec)(const std::string &)        = &DatabaseController::executeReadQuery;
    std::optional<json::array> (DatabaseController::*dbsexec)(const std::string &) = &DatabaseController::executeSearchQuery;

    ///////////////////////////
    template <typename S, typename T>
    /**
     * @brief Generates the SQL statement for a CRUD operation based on the
     * provided entity and SQL statement.
     *
     * This function is responsible for generating the SQL statement to be
     * executed by the DatabaseController. It takes the entity object and the SQL
     * statement function, and returns the generated SQL statement. If the SQL
     * statement cannot be generated, it builds an error response and sends it
     * back to the client.
     *
     * @param response_json The JSON object to store the response data.
     * @param res The Crow response object to send the error response if the SQL
     * statement cannot be generated.
     * @param query The optional string to store the generated SQL statement.
     * @param entity The entity object of type T.
     * @param sqlstatement The SQL statement function of type S.
     * @return true if the SQL statement was successfully generated, false
     * otherwise.
     */
    bool get_sql_statement(crow::response &res, std::optional<std::string> &query, T &entity, S &sqlstatement)
    {
        query = (entity.*sqlstatement)();

        if (!query)
        {
            RestHelper::errorResponse(res, crow::status::BAD_REQUEST, "Failed to synthesize query");
            return false;
        }
        return true;
    }

    template <typename S, typename T>
    /**
     * @brief Executes a CRUD operation using the provided entity, SQL statement,
     * and database controller.
     *
     * This function is responsible for generating the SQL statement, executing
     * the query, and sending the response back to the client. It first generates
     * the SQL statement using the `get_sql_statement` function, and then executes
     * the query using the provided database controller function pointer. If the
     * SQL statement generation or query execution fails, it builds an error
     * response and sends it back to the client.
     *
     * @param res The Crow response object to send the response to the client.
     * @param entity The entity object of type T.
     * @param sqlstatement The SQL statement function of type S.
     * @param f The database controller function pointer to execute the query.
     */
    void cruds(crow::response &res, T &entity, S &sqlstatement, std::optional<json> (DatabaseController::*f)(const std::string &))
    {
        std::optional<json>        query_results_json;
        std::optional<std::string> query;
        std::string                result;
        try
        {
            if (get_sql_statement(res, query, entity, sqlstatement) && query.has_value())
            {
                query_results_json = (*databaseController.*f)(query.value());
            }

            if (query_results_json.has_value() && !query_results_json.value().empty())
            {
                query_results_json.value().dump_pretty(result);
                RestHelper::successResponse(res, crow::status::OK, result);
            }
            else if (query_results_json.has_value() && query_results_json.value().empty())
            {
                RestHelper::errorResponse(res, crow::status::BAD_REQUEST, "Empty query results");
            }
            else
            {
                RestHelper::errorResponse(res, crow::status::BAD_REQUEST, "Failed to execute query");
            }
        }
        catch (const std::exception &e)
        {
            RestHelper::failureResponse(res, e.what());
        }
    }
    template <typename T>
    void addStaff(crow::response &res, T &entity)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlAddStaffStatement;
        cruds(res, entity, sqlstatement, dbexec);
    }

    template <typename T>
    void removeStaff(crow::response &res, T &entity)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlRemoveStaffStatement;
        cruds(res, entity, sqlstatement, dbexec);
    }
};
