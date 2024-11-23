
#pragma once

#include <fmt/core.h>  // Include fmt library for string formatting
#include <fmt/format.h>
#include <fmt/ranges.h>

#include "controllers/databasecontroller/databasecontroller.hpp"
#include "entities/base/client.hpp"
#include "entities/services/clinics/patient/patient.hpp"
#include "utils/global/global.hpp"
#include "utils/jsonhelper/jsonhelper.hpp"
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
            CRITICALMESSAGE
            exit(EXIT_FAILURE);
        }
    }
    virtual ~Controller() = default;

    // CRUDS
    template <typename T, typename CALLBACK>
    void Create(T &entity, CALLBACK &&callback)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlCreateStatement;
        cruds(entity, sqlstatement, dbexec, std::forward<CALLBACK>(callback));
    }

    template <typename T, typename CALLBACK>
    void Read(T &entity, CALLBACK &&callback)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlReadStatement;
        cruds(entity, sqlstatement, dbrexec, std::forward<CALLBACK>(callback));
    }
    template <typename T, typename CALLBACK>
    void Update(T &entity, CALLBACK &&callback)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlUpdateStatement;
        cruds(entity, sqlstatement, dbexec, std::forward<CALLBACK>(callback));
    }
    template <typename T, typename CALLBACK>
    void Delete(T &entity, CALLBACK &&callback)
    {
        if (!entity.template check_id_exists<Types::Delete_t>())
        {
            callback(400, "ID does not exist");
            return;
        }
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlDeleteStatement;
        cruds(entity, sqlstatement, dbexec, std::forward<CALLBACK>(callback));
    }
    template <typename T, typename CALLBACK>
    void Search(T &entity, CALLBACK &&callback)
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
            callback(200, response_json.as<std::string>());
        }
        catch (const std::exception &e)
        {
            CRITICALMESSAGERESPONSE
        }
    }
    template <typename T, typename CALLBACK>
    typename std::enable_if_t<std::is_base_of_v<Client, T>, void> Logout(T &entity, CALLBACK &&callback)
    {
        TokenManager::LoggedClientInfo loggedClientInfo;

        try
        {
            loggedClientInfo.token = std::get<Types::LogoutData>(entity.getData()).token;
            loggedClientInfo.group = entity.getGroupName();

            bool status = tokenManager->ValidateToken(loggedClientInfo);
            if (!status)
            {
                callback(401, "Logout failure.");
                Message::ErrorMessage("Logout failure.");
                return;
            }
            sessionManager->setNowLogoutTime(loggedClientInfo.userID.value(), loggedClientInfo.group.value());
            callback(200, api::v2::JsonHelper::stringify(api::v2::JsonHelper::jsonify("Logout success.")));
        }
        catch (const std::exception &e)
        {
            callback(500, e.what());
            CRITICALMESSAGE
        }
    }

    template <typename T, typename CALLBACK>
    typename std::enable_if_t<std::is_base_of_v<Client, T>, void> Suspend(T &entity, CALLBACK &&callback)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlSuspendStatement;
        cruds(entity, sqlstatement, dbexec, callback);
    }

    template <typename T, typename CALLBACK>
    typename std::enable_if_t<std::is_base_of_v<Client, T>, void> Unsuspend(T &entity, CALLBACK &&callback)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlActivateStatement;
        cruds(entity, sqlstatement, dbexec, callback);
    }

    template <typename T, typename CALLBACK>
    typename std::enable_if_t<std::is_base_of_v<Client, T>, void> GetServices(T &entity, CALLBACK &&callback)
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

            callback(200, api::v2::JsonHelper::stringify(services));
        }
        catch (const std::exception &e)
        {
            CRITICALMESSAGE
        }
    }

    template <typename T, typename CALLBACK>
    std::enable_if_t<std::is_same<T, Patient>::value, void> GetVisits(T &entity, CALLBACK &&callback)
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

            callback(200, api::v2::JsonHelper::stringify(visits));
        }
        catch (const std::exception &e)
        {
            CRITICALMESSAGE
        }
    }

   protected:
    template <typename T>
    std::optional<uint64_t> getNextID(api::v2::Global::HttpError &error)
    {
        try
        {
            json json_nextval = databaseController->executeQuery(fmt::format("SELECT NEXTVAL('{}_id_seq');", T::getTableName()));

            if (json_nextval.empty())
            {
                error.message = fmt::format("nextID from seq function of {} failed, could not create a new ID.", T::getTableName());
                error.code    = 406;
                Message::ErrorMessage(error.message);
                return std::nullopt;
            }

            auto obj = json_nextval.find("nextval");
            if (obj != json_nextval.object_range().end())
            {
                return obj->value().as<uint64_t>();
            }
        }
        catch (const std::exception &e)
        {
            CRITICALMESSAGE
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
    bool get_sql_statement(std::optional<std::string> &query, T &entity, S &sqlstatement, std::string &error)
    {
        query = (entity.*sqlstatement)();
        if (!query.has_value())
        {
            error = fmt::format("Failed to get SQL statement for {}.", entity.getTableName());
            return false;
        }

        return true;
    }

    template <typename S, typename T, typename CALLBACK>
    void cruds(T &entity, S &sqlstatement, std::optional<json> (DatabaseController::*f)(const std::string &), CALLBACK &&callback)
    {
        std::optional<jsoncons::json> results_j;
        std::optional<std::string>    query;
        try
        {
            std::string error;
            if (!get_sql_statement(query, entity, sqlstatement, error))
            {
                callback(400, error);
                return;
            }

            if (query.has_value())
            {
                results_j = (*databaseController.*f)(query.value());
                if (results_j.has_value())
                {
                    callback(200, results_j.value().as<std::string>());
                    return;
                }
                else
                {
                    callback(400, "Failed to execute query");
                }
            }
        }
        catch (const std::exception &e)
        {
            CRITICALMESSAGE
        }
    }

    template <typename T, typename CALLBACK>
    void addStaff(T &entity, CALLBACK &&callback)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlAddStaffStatement;
        cruds(entity, sqlstatement, dbexec, callback);
    }

    template <typename T, typename CALLBACK>
    void removeStaff(T &entity, CALLBACK &&callback)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlRemoveStaffStatement;
        cruds(entity, sqlstatement, dbexec, callback);
    }
};
