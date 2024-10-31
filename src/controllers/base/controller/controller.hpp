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
#include "entities/people/provider.hpp"
#include "entities/people/user.hpp"
#include "entities/services/clinics/patient/patient.hpp"
#include "utils/resthelper/resthelper.hpp"
#include "utils/sessionmanager/sessionmanager.hpp"
#include "utils/tokenmanager/tokenmanager.hpp"
using json = jsoncons::json;

class Controller
{
   public:
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

    void Create(crow::response &res, T &entity)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlCreateStatement;
        cruds(res, entity, sqlstatement, dbexec);
    }

    template <typename T>
    void Read(crow::response &res, T &entity)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlReadStatement;
        cruds(res, entity, sqlstatement, dbrexec);
    }
    template <typename T>
    void Update(crow::response &res, T &entity)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlUpdateStatement;
        cruds(res, entity, sqlstatement, dbexec);
    }
    template <typename T>
    void Delete(crow::response &res, T &entity)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlDeleteStatement;
        cruds(res, entity, sqlstatement, dbexec);
    }
    template <typename T>
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

                if (results_count > std::any_cast<typename T::Search_t>(entity.getData()).limit)
                {
                    response_json["more"] = true;
                    response_json["offset"] =
                        std::any_cast<typename T::Search_t>(entity.getData()).offset + std::any_cast<typename T::Search_t>(entity.getData()).limit;
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
            RestHelper::failureResponse(res, e.what());
        }
    }
    template <typename T>
    typename std::enable_if<std::is_same<T, User>::value || std::is_same<T, Provider>::value, void>::type Logout(crow::response &res, T &entity)
    {
        TokenManager::LoggedUserInfo loggedUserInfo;

        try
        {
            loggedUserInfo.token = std::any_cast<Client::LogoutData>(entity.getData()).token;
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
    typename std::enable_if_t<std::is_base_of_v<Client, T>, void> Suspend(crow::response &res, T &entity)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlSuspendStatement;
        cruds(res, entity, sqlstatement, dbexec);
    }

    template <typename T>
    typename std::enable_if_t<std::is_base_of_v<Client, T>, void> Unsuspend(crow::response &res, T &entity)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlActivateStatement;
        cruds(res, entity, sqlstatement, dbexec);
    }

    template <typename T>
    typename std::enable_if_t<std::is_base_of_v<Client, T>, void> GetServices(crow::response &res, T &entity)
    {
        json                       services;
        std::string                response;
        std::optional<std::string> query;

        try
        {
            query = entity.getSqlGetServicesStatement();

            if (query)
            {
                services = databaseController->executeSearchQuery(query.value());
            }

            services.dump_pretty(response);
            RestHelper::successResponse(res, crow::status::OK, response);
        }
        catch (const std::exception &e)
        {
            RestHelper::failureResponse(res, e.what());
        }
    }

    template <typename T>
    typename std::enable_if<std::is_same<T, Patient>::value, void>::type GetVisits(crow::response &res, T &entity)
    {
        json                       visits;
        std::string                response;
        std::optional<std::string> query;

        try
        {
            query = entity.getSqlGetVisitsStatement();

            if (query)
            {
                visits = databaseController->executeSearchQuery(query.value());
            }

            visits.dump_pretty(response);
            RestHelper::successResponse(res, crow::status::OK, response);
        }
        catch (const std::exception &e)
        {
            RestHelper::failureResponse(res, e.what());
        }
    }

   protected:
    template <typename T>
    std::optional<uint64_t> getNextID()
    {
        try
        {
            T    entity;
            json json_nextval = databaseController->executeQuery(fmt::format("SELECT NEXTVAL('{}_id_seq');", entity.getTableName()));

            if (json_nextval.empty())
            {
                std::cerr << fmt::format("json_nextval is empty\n");
            }

            auto obj = json_nextval.find("nextval");
            if (obj != json_nextval.object_range().end())
            {
                return obj->value().as<uint64_t>();
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << fmt::format("Failed: {}\n", e.what());
        }
        return std::nullopt;
    }
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
