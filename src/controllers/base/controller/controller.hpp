
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
#include "drogon/utils/coroutine.h"
#include "database/asyncdbclient.hpp"
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
        catch (const std::exception& e)
        {
            CRITICALMESSAGE
            throw std::runtime_error("Failed to initialize database controller");
        }
    }

    Controller& operator=(const Controller&) = default;
    Controller(const Controller&)            = default;
    Controller& operator=(Controller&&)      = default;
    Controller(Controller&&)                 = default;

    virtual ~Controller() = default;

    // CRUDS
    template <typename T>
    void Create(T& entity, CALLBACK_&& callback)
    {
        std::optional<std::string> (T::*sqlstatement)(pqxx::params&) = &T::getSqlCreateStatement;
        cruds(entity, sqlstatement, dbexec, std::forward<CALLBACK_>(std::move(callback)));
    }

    template <typename T>
    drogon::Task<void> CreateAsync(T& entity, CALLBACK_&& callback)
    {
        std::optional<std::string> (T::*sqlstatement)(pqxx::params&) = &T::getSqlCreateStatement;
        co_await crudsAsync(entity, sqlstatement, &DatabaseController::executeQueryCoro, std::forward<CALLBACK_>(std::move(callback)));
    }

    template <typename T>
    void Read(T& entity, CALLBACK_&& callback)
    {
        std::optional<std::string> (T::*sqlstatement)(pqxx::params&) = &T::getSqlReadStatement;
        cruds(entity, sqlstatement, dbrexec, std::forward<CALLBACK_>(std::move(callback)));
    }
    template <typename T>
    drogon::Task<void> ReadAsync(T& entity, CALLBACK_&& callback)
    {
        std::optional<std::string> (T::*sqlstatement)(pqxx::params&) = &T::getSqlReadStatement;
        co_await crudsAsync(entity, sqlstatement, &DatabaseController::executeReadQueryCoro, std::forward<CALLBACK_>(std::move(callback)));
    }
    template <typename T>
    void Update(T& entity, CALLBACK_&& callback)
    {
        std::optional<std::string> (T::*sqlstatement)(pqxx::params&) = &T::getSqlUpdateStatement;
        cruds(entity, sqlstatement, dbexec, std::forward<CALLBACK_>(std::move(callback)));
    }

    template <typename T>
    drogon::Task<void> UpdateAsync(T& entity, CALLBACK_&& callback)
    {
        std::optional<std::string> (T::*sqlstatement)(pqxx::params&) = &T::getSqlUpdateStatement;
        co_await crudsAsync(entity, sqlstatement, &DatabaseController::executeQueryCoro, std::forward<CALLBACK_>(std::move(callback)));
    }
    template <typename T>
    void Delete(T& entity, CALLBACK_&& callback)
    {
        std::optional<std::string> (T::*sqlstatement)(pqxx::params&) = &T::getSqlDeleteStatement;
        cruds(entity, sqlstatement, dbexec, std::forward<CALLBACK_>(std::move(callback)));
    }

    template <typename T>
    drogon::Task<void> DeleteAsync(T& entity, CALLBACK_&& callback)
    {
        std::optional<std::string> (T::*sqlstatement)(pqxx::params&) = &T::getSqlDeleteStatement;
        co_await crudsAsync(entity, sqlstatement, &DatabaseController::executeQueryCoro, std::forward<CALLBACK_>(std::move(callback)));
    }
    template <typename T>
    void Search(T& entity, CALLBACK_&& callback)
    {
        jsoncons::json response_json;
        response_json["more"]   = false;
        response_json["offset"] = 0;

        std::optional<jsoncons::json> query_results_json_array;
        std::optional<std::string>    query;

        try
        {
            pqxx::params params;
            query = entity.getSqlSearchStatement(params);

            if (query.has_value())
            {
                query_results_json_array = databaseController->executeSearchQuery(query.value(), params);

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
            if (query_results_json_array.has_value())
            {
                response_json["results"] = std::move(query_results_json_array.value());
            }
            else
            {
                response_json["results"] = jsoncons::json::array();
            }

            std::move(callback)(api::v2::Http::Status::OK, response_json.as<std::string>());
        }
        catch (const std::exception& e)
        {
            CRITICALMESSAGERESPONSE
        }
    }
    template <typename T>
    drogon::Task<void> SearchAsync(T& entity, CALLBACK_&& callback)
    {
        jsoncons::json response_json;
        response_json["more"]   = false;
        response_json["offset"] = 0;

        std::optional<jsoncons::json> query_results_json_array;
        std::optional<std::string>    query;

        try
        {
            pqxx::params params;
            query = entity.getSqlSearchStatement(params);

            if (query.has_value())
            {
                query_results_json_array = co_await databaseController->executeSearchQueryCoro(query.value(), params);

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
            if (query_results_json_array.has_value())
            {
                response_json["results"] = std::move(query_results_json_array.value());
            }
            else
            {
                response_json["results"] = jsoncons::json::array();
            }

            std::move(callback)(api::v2::Http::Status::OK, response_json.as<std::string>());
        }
        catch (const std::exception& e)
        {
            CRITICALMESSAGERESPONSE
        }
        co_return;
    }
    template <typename T>
    void Suspend(T& entity, CALLBACK_&& callback)
        requires(std::is_base_of_v<Client, T>)
    {
        std::optional<std::string> (T::*sqlstatement)(pqxx::params&) = &T::getSqlSuspendStatement;
        cruds(entity, sqlstatement, dbexec, std::move(callback));
    }

    template <typename T>
    void Unsuspend(T& entity, CALLBACK_&& callback)
        requires(std::is_base_of_v<Client, T>)
    {
        std::optional<std::string> (T::*sqlstatement)(pqxx::params&) = &T::getSqlActivateStatement;
        cruds(entity, sqlstatement, dbexec, std::move(callback));
    }

    template <typename T>
    drogon::Task<void> SuspendAsync(T& entity, CALLBACK_&& callback)
        requires(std::is_base_of_v<Client, T>)
    {
        std::optional<std::string> (T::*sqlstatement)(pqxx::params&) = &T::getSqlSuspendStatement;
        co_await crudsAsync(entity, sqlstatement, &DatabaseController::executeQueryCoro, std::move(callback));
    }

    template <typename T>
    drogon::Task<void> UnsuspendAsync(T& entity, CALLBACK_&& callback)
        requires(std::is_base_of_v<Client, T>)
    {
        std::optional<std::string> (T::*sqlstatement)(pqxx::params&) = &T::getSqlActivateStatement;
        co_await crudsAsync(entity, sqlstatement, &DatabaseController::executeQueryCoro, std::move(callback));
    }

    template <typename T>
    void GetServices(T& entity, CALLBACK_&& callback)
        requires(std::is_base_of_v<Client, T>)
    {
        std::optional<jsoncons::json> services;
        std::optional<std::string>    query;

        try
        {
            pqxx::params params;
            query = entity.getSqlGetServicesStatement(params);

            if (query.has_value())
            {
                services = databaseController->executeSearchQuery(query.value(), params);

                if (services.has_value())
                {
                    std::move(callback)(api::v2::Http::Status::OK, api::v2::JsonHelper::stringify(services.value()));
                    return;
                }
                std::move(callback)(api::v2::Http::Status::OK, "[]");
            }
        }
        catch (const std::exception& e)
        {
            CRITICALMESSAGERESPONSE
        }
    }

    template <typename T>
    drogon::Task<void> GetServicesAsync(T& entity, CALLBACK_&& callback)
        requires(std::is_base_of_v<Client, T>)
    {
        std::optional<jsoncons::json> services;
        std::optional<std::string>    query;

        try
        {
            pqxx::params params;
            query = entity.getSqlGetServicesStatement(params);

            if (query.has_value())
            {
                auto db = Store::cachedObject<DatabaseController>();
                services = co_await db->executeSearchQueryCoro(query.value(), params);

                if (services.has_value())
                {
                    std::move(callback)(api::v2::Http::Status::OK, api::v2::JsonHelper::stringify(services.value()));
                    co_return;
                }
                std::move(callback)(api::v2::Http::Status::OK, "[]");
            }
        }
        catch (const std::exception& e)
        {
            CRITICALMESSAGERESPONSE
        }
    }

    template <typename T>
    void GetVisits(T& entity, CALLBACK_&& callback)
        requires std::is_same<T, Patient>::value
    {
        jsoncons::json             visits;
        std::optional<std::string> query;

        try
        {
            pqxx::params params;
            query = entity.getSqlGetVisitsStatement(params);

            if (query.has_value())
            {
                visits = databaseController->executeSearchQuery(query.value(), params);
            }

            std::move(callback)(api::v2::Http::Status::OK, api::v2::JsonHelper::stringify(visits));
        }
        catch (const std::exception& e)
        {
            CRITICALMESSAGERESPONSE
        }
    }

    template <typename T>
    drogon::Task<void> GetVisitsAsync(T& entity, CALLBACK_&& callback)
        requires std::is_same<T, Patient>::value
    {
        jsoncons::json             visits;
        std::optional<std::string> query;

        try
        {
            pqxx::params params;
            query = entity.getSqlGetVisitsStatement(params);

            if (query.has_value())
            {
                auto db = Store::cachedObject<DatabaseController>();
                visits = co_await db->executeSearchQueryCoro(query.value(), params);
            }

            std::move(callback)(api::v2::Http::Status::OK, api::v2::JsonHelper::stringify(visits));
        }
        catch (const std::exception& e)
        {
            CRITICALMESSAGERESPONSE
        }
    }

   private:
    std::shared_ptr<DatabaseController> databaseController;
    std::optional<jsoncons::json> (DatabaseController::*dbexec)(const std::string&, const pqxx::params&)  = &DatabaseController::executeQuery;
    std::optional<jsoncons::json> (DatabaseController::*dbrexec)(const std::string&, const pqxx::params&) = &DatabaseController::executeReadQuery;

   protected:
    ///////////////////////////
    // Runs one entity statement: it builds the SQL (placeholders) and its params, executes it and reports the result.
    template <typename S, typename T>
    void cruds(T& entity, S& sqlstatement, std::optional<jsoncons::json> (DatabaseController::*func)(const std::string&, const pqxx::params&),
        CALLBACK_&& callback)
    {
        std::optional<jsoncons::json> results_j;
        pqxx::params                  params;
        try
        {
            std::optional<std::string> query = (entity.*sqlstatement)(params);
            if (!query.has_value())
            {
                std::move(callback)(api::v2::Http::Status::BAD_REQUEST, fmt::format("Failed to get SQL statement for {}.", entity.getTableName()));
                return;
            }

            results_j = (*databaseController.*func)(query.value(), params);
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
        catch (const std::exception& e)
        {
            CRITICALMESSAGERESPONSE
        }
    }

    template <typename S, typename T>
    drogon::Task<void> crudsAsync(T& entity, S& sqlstatement, drogon::Task<std::optional<jsoncons::json>> (DatabaseController::*func)(const std::string&, const pqxx::params&), CALLBACK_&& callback)
    {
        std::optional<jsoncons::json> results_j;
        pqxx::params                  params;
        try
        {
            std::optional<std::string> query = (entity.*sqlstatement)(params);
            if (!query.has_value())
            {
                std::move(callback)(api::v2::Http::Status::BAD_REQUEST, fmt::format("Failed to get SQL statement for {}.", entity.getTableName()));
                co_return;
            }

            auto db = Store::cachedObject<DatabaseController>();
            results_j = co_await std::invoke(func, db.get(), query.value(), params);
            if (results_j.has_value())
            {
                if (!results_j->empty())
                {
                    std::move(callback)(api::v2::Http::Status::OK, results_j.value().as<std::string>());
                    co_return;
                }

                std::move(callback)(api::v2::Http::Status::BAD_REQUEST, "Query returned empty result, please recheck your parameters.");
                co_return;
            }

            std::move(callback)(api::v2::Http::Status::BAD_REQUEST, "Failed to create sql query");
        }
        catch (const std::exception& e)
        {
            CRITICALMESSAGERESPONSE
        }
    }

    template <typename T>
    void addStaff(T& entity, CALLBACK_&& callback)
    {
        std::optional<std::string> (T::*sqlstatement)(pqxx::params&) = &T::getSqlAddStaffStatement;
        cruds(entity, sqlstatement, dbexec, std::move(callback));
    }

    template <typename T>
    void removeStaff(T& entity, CALLBACK_&& callback)
    {
        std::optional<std::string> (T::*sqlstatement)(pqxx::params&) = &T::getSqlRemoveStaffStatement;
        cruds(entity, sqlstatement, dbexec, std::move(callback));
    }

    template <typename T>
    drogon::Task<void> addStaffAsync(T& entity, CALLBACK_&& callback)
    {
        std::optional<std::string> (T::*sqlstatement)(pqxx::params&) = &T::getSqlAddStaffStatement;
        co_await crudsAsync(entity, sqlstatement, &DatabaseController::executeQueryCoro, std::move(callback));
    }

    template <typename T>
    drogon::Task<void> removeStaffAsync(T& entity, CALLBACK_&& callback)
    {
        std::optional<std::string> (T::*sqlstatement)(pqxx::params&) = &T::getSqlRemoveStaffStatement;
        co_await crudsAsync(entity, sqlstatement, &DatabaseController::executeQueryCoro, std::move(callback));
    }
};
