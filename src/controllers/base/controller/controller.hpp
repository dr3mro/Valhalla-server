
#pragma once
#include <drogon/drogon.h>
#include <fmt/core.h>  // Include fmt library for string formatting
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <jsoncons/json.hpp>

#include "controllers/databasecontroller/databasecontroller.hpp"
#include "entities/base/client.hpp"
#include "entities/base/entity.hpp"
#include "entities/services/clinics/patient/patient.hpp"
#include "utils/helper/helper.hpp"
#include "utils/message/message.hpp"
#include "utils/sessionmanager/sessionmanager.hpp"
#include "utils/tokenmanager/tokenmanager.hpp"

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
            Message::ErrorMessage(fmt::format("Exception in Controller constructor."));
            Message::CriticalMessage(e.what());
            exit(EXIT_FAILURE);
        }
    }
    virtual ~Controller() = default;

    // CRUDS
    template <typename T>

    void Create(T &entity, std::function<void(const drogon::HttpResponsePtr &)> &callback)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlCreateStatement;
        cruds(entity, sqlstatement, dbexec, callback);
    }

    template <typename T>
    void Read(T &entity, std::function<void(const drogon::HttpResponsePtr &)> &callback)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlReadStatement;
        cruds(entity, sqlstatement, dbrexec, callback);
    }
    template <typename T>
    void Update(T &entity, std::function<void(const drogon::HttpResponsePtr &)> &callback)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlUpdateStatement;
        cruds(entity, sqlstatement, dbexec, callback);
    }
    template <typename T>
    void Delete(T &entity, std::function<void(const drogon::HttpResponsePtr &)> &callback)
    {
        if (!entity.template check_id_exists<Types::Delete_t>())
        {
            Helper::errorResponse(drogon::k400BadRequest, "ID does not exist", callback);
            return;
        }
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlDeleteStatement;
        cruds(entity, sqlstatement, dbexec, callback);
    }
    template <typename T>
    void Search(T &entity, std::function<void(const drogon::HttpResponsePtr &)> &callback)
    {
        jsoncons::json             response_json;
        json                       query_results_json;
        std::optional<std::string> query;

        try
        {
            query = entity.getSqlSearchStatement();

            if (query)
            {
                query_results_json   = databaseController->executeSearchQuery(query.value());
                size_t results_count = query_results_json.size();

                if (results_count > std::get<Types::Search_t>(entity.getData()).limit)
                {
                    response_json["more"]   = true;
                    response_json["offset"] = std::get<Types::Search_t>(entity.getData()).offset + std::get<Types::Search_t>(entity.getData()).limit;
                    query_results_json.erase(query_results_json.array_range().end() - 1);
                }
                else
                {
                    response_json["more"]   = false;
                    response_json["offset"] = 0;
                }
            }

            response_json["results"] = query_results_json;
            Helper::successResponse(Helper::stringify(response_json), callback);
        }
        catch (const std::exception &e)
        {
            Helper::failureResponse(e.what(), callback);
        }
    }
    template <typename T>
    typename std::enable_if_t<std::is_base_of_v<Client, T>, void> Logout(T &entity, std::function<void(const drogon::HttpResponsePtr &)> &callback)
    {
        TokenManager::LoggedClientInfo loggedClientInfo;

        try
        {
            loggedClientInfo.token = std::get<Types::LogoutData>(entity.getData()).token;
            loggedClientInfo.group = entity.getGroupName();

            bool status = tokenManager->ValidateToken(loggedClientInfo);
            if (!status)
            {
                Helper::errorResponse(drogon::k401Unauthorized, "Logout failure.", callback);
                return;
            }
            sessionManager->setNowLogoutTime(loggedClientInfo.userID.value(), loggedClientInfo.group.value());
            Helper::successResponse(Helper::stringify(Helper::jsonify("Logout success.")), callback);
        }
        catch (const std::exception &e)
        {
            Helper::failureResponse(e.what(), callback);
        }
    }

    template <typename T>
    typename std::enable_if_t<std::is_base_of_v<Client, T>, void> Suspend(T &entity, std::function<void(const drogon::HttpResponsePtr &)> &callback)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlSuspendStatement;
        cruds(entity, sqlstatement, dbexec, callback);
    }

    template <typename T>
    typename std::enable_if_t<std::is_base_of_v<Client, T>, void> Unsuspend(T &entity, std::function<void(const drogon::HttpResponsePtr &)> &callback)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlActivateStatement;
        cruds(entity, sqlstatement, dbexec, callback);
    }

    template <typename T>
    typename std::enable_if_t<std::is_base_of_v<Client, T>, void> GetServices(T                                                    &entity,
                                                                              std::function<void(const drogon::HttpResponsePtr &)> &callback)
    {
        json                       services;
        std::optional<std::string> query;

        try
        {
            query = entity.getSqlGetServicesStatement();

            if (query)
            {
                services = databaseController->executeSearchQuery(query.value());
            }

            Helper::successResponse(Helper::stringify(services), callback);
        }
        catch (const std::exception &e)
        {
            Helper::failureResponse(e.what(), callback);
        }
    }

    template <typename T>
    std::enable_if_t<std::is_same<T, Patient>::value, void> GetVisits(T &entity, std::function<void(const drogon::HttpResponsePtr &)> &&callback)
    {
        json                       visits;
        std::optional<std::string> query;

        try
        {
            query = entity.getSqlGetVisitsStatement();

            if (query)
            {
                visits = databaseController->executeSearchQuery(query.value());
            }

            Helper::successResponse(Helper::stringify(visits), callback);
        }
        catch (const std::exception &e)
        {
            Helper::failureResponse(e.what(), callback);
        }
    }

   protected:
    template <typename T>
    std::optional<uint64_t> getNextID()
    {
        try
        {
            json json_nextval = databaseController->executeQuery(fmt::format("SELECT NEXTVAL('{}_id_seq');", T::getTableName()));

            if (json_nextval.empty())
            {
                Message::ErrorMessage("json_nextval is empty.");
            }

            auto obj = json_nextval.find("nextval");
            if (obj != json_nextval.object_range().end())
            {
                return obj->value().as<uint64_t>();
            }
        }
        catch (const std::exception &e)
        {
            Message::CriticalMessage(fmt::format("Failed: {}.", e.what()));
        }
        return std::nullopt;
    }

    std::shared_ptr<DatabaseController> databaseController;
    std::shared_ptr<SessionManager>     sessionManager;
    std::shared_ptr<TokenManager>       tokenManager;

    std::optional<jsoncons::json> (DatabaseController::*dbexec)(const std::string &)  = &DatabaseController::executeQuery;
    std::optional<jsoncons::json> (DatabaseController::*dbrexec)(const std::string &) = &DatabaseController::executeReadQuery;
    std::optional<json::array> (DatabaseController::*dbsexec)(const std::string &)    = &DatabaseController::executeSearchQuery;

    ///////////////////////////
    template <typename S, typename T>
    bool get_sql_statement(std::optional<std::string> &query, T &entity, S &sqlstatement,
                           std::function<void(const drogon::HttpResponsePtr &)> &callback)
    {
        query = (entity.*sqlstatement)();

        if (!query)
        {
            Helper::errorResponse(drogon::k400BadRequest, "Failed to synthesize query", callback);
            return false;
        }
        return true;
    }

    template <typename S, typename T>
    void cruds(T &entity, S &sqlstatement, std::optional<json> (DatabaseController::*f)(const std::string &),
               std::function<void(const drogon::HttpResponsePtr &)> &callback)
    {
        std::optional<jsoncons::json> query_results_json;
        std::optional<std::string>    query;
        try
        {
            if (get_sql_statement(query, entity, sqlstatement, callback) && query.has_value())
            {
                query_results_json = (*databaseController.*f)(query.value());
            }

            if (query_results_json.has_value() && !query_results_json.value().empty())
            {
                Helper::successResponse(Helper::stringify(query_results_json.value()), callback);
            }
            else if (query_results_json.has_value() && query_results_json.value().empty())
            {
                Helper::errorResponse(drogon::k400BadRequest, "Empty query results", callback);
            }
            else
            {
                Helper::errorResponse(drogon::k400BadRequest, "Failed to execute query", callback);
            }
        }
        catch (const std::exception &e)
        {
            Helper::failureResponse(e.what(), callback);
        }
    }
    template <typename T>
    void addStaff(T &entity, std::function<void(const drogon::HttpResponsePtr &)> &callback)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlAddStaffStatement;
        cruds(entity, sqlstatement, dbexec, callback);
    }

    template <typename T>
    void removeStaff(T &entity, std::function<void(const drogon::HttpResponsePtr &)> &callback)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlRemoveStaffStatement;
        cruds(entity, sqlstatement, dbexec, callback);
    }
};
