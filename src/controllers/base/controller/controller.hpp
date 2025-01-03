
#pragma once

#include <fmt/core.h>  // Include fmt library for string formatting
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <jsoncons/basic_json.hpp>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "controllers/databasecontroller/databasecontroller.hpp"
#include "entities/base/client.hpp"
#include "entities/base/types.hpp"
#include "entities/services/clinics/patient/patient.hpp"
#include "gatekeeper/gatekeeper.hpp"
#include "store/store.hpp"
#include "utils/global/callback.hpp"
#include "utils/global/global.hpp"
#include "utils/global/http.hpp"
#include "utils/jsonhelper/jsonhelper.hpp"
#include "utils/message/message.hpp"

using Search_t   = api::v2::Types::Search_t;
using Client     = api::v2::Client;
using GateKeeper = api::v2::GateKeeper;

class Controller
{
   public:
    Controller()
    {
        try
        {
            databaseController = Store::getObject<DatabaseController>();
        }
        catch (const std::exception &e)
        {
            CRITICALMESSAGE
            throw std::runtime_error("Failed to initialize database controller");
        }
    }

    Controller &operator=(const Controller &) = default;
    Controller(const Controller &)            = default;
    Controller &operator=(Controller &&)      = default;
    Controller(Controller &&)                 = default;

    virtual ~Controller() = default;

    // CRUDS
    template <typename T>
    void Create(T &entity, CALLBACK_ &&callback)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlCreateStatement;
        cruds(entity, sqlstatement, dbexec, std::forward<CALLBACK_>(std::move(callback)));
    }

    template <typename T>
    void Read(T &entity, CALLBACK_ &&callback)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlReadStatement;
        cruds(entity, sqlstatement, dbrexec, std::forward<CALLBACK_>(std::move(callback)));
    }
    template <typename T>
    void Update(T &entity, CALLBACK_ &&callback)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlUpdateStatement;
        cruds(entity, sqlstatement, dbexec, std::forward<CALLBACK_>(std::move(callback)));
    }
    template <typename T>
    void Delete(T &entity, CALLBACK_ &&callback)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlDeleteStatement;
        cruds(entity, sqlstatement, dbexec, std::forward<CALLBACK_>(std::move(callback)));
    }
    template <typename T>
    void Search(T &entity, CALLBACK_ &&callback)
    {
        jsoncons::json response_json;
        response_json["more"]   = false;
        response_json["offset"] = 0;

        std::optional<jsoncons::json> query_results_json_array;
        std::optional<std::string>    query;

        try
        {
            query = entity.getSqlSearchStatement();

            if (query.has_value())
            {
                bool isSqlInjection      = false;
                query_results_json_array = databaseController->executeSearchQuery(query.value(), isSqlInjection);

                if (isSqlInjection)
                {
                    std::move(callback)(api::v2::Http::Status::BAD_REQUEST, "A Sql Injection pattern is detected in generated query.");
                    return;
                }

                if (query_results_json_array.has_value() && !query_results_json_array->empty())
                {
                    size_t results_count = query_results_json_array->size();
                    if (results_count > std::get<Search_t>(entity.getData()).limit)
                    {
                        response_json["more"]   = true;
                        response_json["offset"] = std::get<Search_t>(entity.getData()).offset + std::get<Search_t>(entity.getData()).limit;
                        query_results_json_array->erase(query_results_json_array->array_range().end() - 1);
                    }
                }
            }
            response_json["results"] = query_results_json_array.value();
            std::move(callback)(api::v2::Http::Status::OK, response_json.as<std::string>());
        }
        catch (const std::exception &e)
        {
            CRITICALMESSAGERESPONSE
        }
    }

    template <typename T>
    void Suspend(T &entity, CALLBACK_ &&callback)
        requires(std::is_base_of_v<Client, T>)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlSuspendStatement;
        cruds(entity, sqlstatement, dbexec, std::move(callback));
    }

    template <typename T>
    void Unsuspend(T &entity, CALLBACK_ &&callback)
        requires(std::is_base_of_v<Client, T>)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlActivateStatement;
        cruds(entity, sqlstatement, dbexec, std::move(callback));
    }

    template <typename T>
    void GetServices(T &entity, CALLBACK_ &&callback)
        requires(std::is_base_of_v<Client, T>)
    {
        std::optional<jsoncons::json> services;
        std::optional<std::string>    query;

        try
        {
            query = entity.getSqlGetServicesStatement();

            if (query.has_value())
            {
                bool isSqlInjection = false;

                services = databaseController->executeSearchQuery(query.value(), isSqlInjection);
                if (isSqlInjection)
                {
                    std::move(callback)(api::v2::Http::Status::BAD_REQUEST, "A Sql Injection pattern is detected in generated query.");
                    return;
                }

                if (services.has_value())
                {
                    std::move(callback)(api::v2::Http::Status::OK, api::v2::JsonHelper::stringify(services.value()));
                    return;
                }
                std::move(callback)(api::v2::Http::Status::OK, "[]");
            }
        }
        catch (const std::exception &e)
        {
            CRITICALMESSAGE
        }
    }

    template <typename T>
    void GetVisits(T &entity, CALLBACK_ &&callback)
        requires std::is_same<T, Patient>::value
    {
        jsoncons::json             visits;
        std::optional<std::string> query;

        try
        {
            query = entity.getSqlGetVisitsStatement();

            if (query.has_value())
            {
                bool isSqlInjection = false;
                visits              = databaseController->executeSearchQuery(query.value(), isSqlInjection);

                if (isSqlInjection)
                {
                    std::move(callback)(api::v2::Http::Status::BAD_REQUEST, "A Sql Injection pattern is detected in generated query.");
                    return;
                }
            }

            std::move(callback)(api::v2::Http::Status::OK, api::v2::JsonHelper::stringify(visits));
        }
        catch (const std::exception &e)
        {
            CRITICALMESSAGE
        }
    }

   private:
    std::shared_ptr<DatabaseController> databaseController;
    std::shared_ptr<GateKeeper>         gateKeeper_                                           = Store::getObject<GateKeeper>();
    std::optional<jsoncons::json> (DatabaseController::*dbexec)(const std::string &, bool &)  = &DatabaseController::executeQuery;
    std::optional<jsoncons::json> (DatabaseController::*dbrexec)(const std::string &, bool &) = &DatabaseController::executeReadQuery;

   protected:
    template <typename T>
    std::optional<uint64_t> getNextID(api::v2::Http::Error &error)
    {
        try
        {
            bool                          isSqlInjection = false;
            std::optional<jsoncons::json> json_nextval =
                databaseController->executeQuery(fmt::format("SELECT NEXTVAL('{}_id_seq');", T::getTableName()), isSqlInjection);

            if (isSqlInjection)
            {
                error.message = "A Sql Injection pattern is detected in generated query.";
                error.code    = api::v2::Http::Status::BAD_REQUEST;
                return std::nullopt;
            }

            if (!json_nextval.has_value() || json_nextval->empty())
            {
                error.message = fmt::format(
                    "nextID from seq function of {} failed, could not create a "
                    "new ID.",
                    T::getTableName());
                error.code = api::v2::Http::Status::CONFLICT;
                Message::ErrorMessage(error.message);
                return std::nullopt;
            }

            auto obj = json_nextval->find("nextval");
            if (obj != json_nextval->object_range().end())
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

    ///////////////////////////
    template <typename S, typename T>
    bool get_sql_statement(std::optional<std::string> &query, T &entity, S &sqlstatement, std::string &error)
    {
        query = (entity.*sqlstatement)();
        if (query.has_value() && gateKeeper_->isQuerySqlInjection(query.value()))
        {
            error = "A Sql Injection pattern is detected in generated query.";
            return false;
        }
        if (!query.has_value())
        {
            error = fmt::format("Failed to get SQL statement for {}.", entity.getTableName());
            return false;
        }

        return true;
    }

    template <typename S, typename T>
    void cruds(T &entity, S &sqlstatement, std::optional<jsoncons::json> (DatabaseController::*func)(const std::string &, bool &), CALLBACK_ &&callback)
    {
        std::optional<jsoncons::json> results_j;
        std::optional<std::string>    query;
        try
        {
            std::string error;
            if (!get_sql_statement(query, entity, sqlstatement, error))
            {
                std::move(callback)(api::v2::Http::Status::BAD_REQUEST, error);
                return;
            }

            if (query.has_value())
            {
                bool isSqlInjection = false;
                results_j           = (*databaseController.*func)(query.value(), isSqlInjection);
                if (results_j.has_value())
                {
                    if (!results_j->empty())
                    {
                        std::move(callback)(api::v2::Http::Status::OK, results_j.value().as<std::string>());
                        return;
                    }

                    std::move(callback)(api::v2::Http::Status::BAD_REQUEST, "Query returned empty result, please recheck your parameters.");
                    return;
                }

                std::move(callback)(api::v2::Http::Status::BAD_REQUEST, "Failed to create sql query");
            }
        }
        catch (const std::exception &e)
        {
            CRITICALMESSAGE
        }
    }

    template <typename T>
    void addStaff(T &entity, CALLBACK_ &&callback)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlAddStaffStatement;
        cruds(entity, sqlstatement, dbexec, std::move(callback));
    }

    template <typename T>
    void removeStaff(T &entity, CALLBACK_ &&callback)
    {
        std::optional<std::string> (T::*sqlstatement)() = &T::getSqlRemoveStaffStatement;
        cruds(entity, sqlstatement, dbexec, std::move(callback));
    }
};
