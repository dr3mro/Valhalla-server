#include "databasecontroller.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <cstdint>
#include <cstdlib>
#include <jsoncons/basic_json.hpp>
#include <mutex>
#include <optional>
#include <pqxx/pqxx>
#include <string>
#include <string_view>
#include <unordered_set>

#include "configurator/configurator.hpp"
#include "database/database.hpp"
#include "database/databaseconnectionpool.hpp"
#include "database/watchdog.hpp"
#include "store/store.hpp"
#include "utils/global/types.hpp"

DatabaseController::DatabaseController() {}

void DatabaseController::ensureBackends()
{
    // Lazily create the connection pool and watchdog on first use instead of
    // in the constructor: the pool constructor calls exit(EXIT_FAILURE) when
    // PostgreSQL is unreachable, which made the whole class unconstructible in
    // hermetic unit tests. The server still connects at startup, because
    // Server's constructor eagerly creates DatabaseSchema, which constructs
    // the (singleton) DatabaseController through Store and then performs the
    // first query (schema population) during boot.
    //
    // Every query calls this, so the common case (already created) is one atomic load. The mutex is only taken during the first
    // calls; the release/acquire pair makes the pointers written below visible to every thread that sees the flag set.
    if (backendsReady_.load(std::memory_order_acquire))
    {
        return;
    }

    static std::mutex backendInitMutex;
    std::scoped_lock  lock(backendInitMutex);
    if (backendsReady_.load(std::memory_order_relaxed))
    {
        return;
    }
    if (!databaseConnectionPool_)
    {
        // Hermetic unit-test seam: when VALHALLA_TEST_NO_DB is set (the unit
        // test binary sets it before any Configurator is created), the pool
        // and watchdog are never created. Every query path then reports
        // "database unavailable" (std::nullopt) deterministically, with no
        // sockets opened and no retries. Production never sets the variable,
        // so behaviour is unchanged there.
        if (!testing_config_->no_db)
        {
            databaseConnectionPool_ = Store::getObject<DatabaseConnectionPool>();
            watchDog_               = Store::getObject<WatchDog>();
        }
    }
    backendsReady_.store(true, std::memory_order_release);
}

std::optional<jsoncons::json> DatabaseController::executeQuery(const std::string& query, const pqxx::params& params)
{
    return executer<jsoncons::json>(&Database::executeQuery<jsoncons::json, pqxx::work>, query, params);
}

std::optional<jsoncons::json> DatabaseController::executeReadQuery(const std::string& query, const pqxx::params& params)
{
    return executer<jsoncons::json>(&Database::executeQuery<jsoncons::json, pqxx::nontransaction>, query, params);
}

std::optional<jsoncons::json::array> DatabaseController::executeSearchQuery(const std::string& query, const pqxx::params& params)
{
    return executer<jsoncons::json::array>(&Database::executeQuery<jsoncons::json::array, pqxx::nontransaction>, query, params);
}

std::optional<std::string> DatabaseController::doReadQuery(const std::string& query, const pqxx::params& params)
{
    return executer<std::string>(&Database::doSimpleQuery<std::string>, query, params);
}

std::optional<bool> DatabaseController::checkItemExists(const std::string& table, const std::string& column, const std::string& value)
{
    return executer<bool>(&Database::checkExists, table, column, value);
}

std::optional<jsoncons::json> DatabaseController::getPasswordHashForUserName(const std::string& username, const std::string& tablename)
{
    // Identifiers cannot be bound: only the two client tables are accepted.
    if (tablename != "users" && tablename != "providers")
    {
        Message::ErrorMessage(fmt::format("Unknown client table: {}", tablename));
        return std::nullopt;
    }
    const std::string query = fmt::format("SELECT id,password,active FROM {} WHERE username = $1 LIMIT 1;", tablename);
    return executer<jsoncons::json>(&Database::executeQuery<jsoncons::json, pqxx::nontransaction>, query, pqxx::params{username});
}

std::optional<std::unordered_set<api::v2::ColumnInfo>> DatabaseController::getTableSchema(const std::string& tableName)
{
    return executer<std::unordered_set<api::v2::ColumnInfo>>(&Database::getTableSchema, tableName);
}

std::optional<std::unordered_set<std::string>> DatabaseController::getAllTables() { return executer<std::unordered_set<std::string>>(&Database::getAllTables); }

std::optional<jsoncons::json> DatabaseController::getPermissions(const std::string& query)
{
    return executer<jsoncons::json>(&Database::executeQuery<jsoncons::json, pqxx::nontransaction>, query, pqxx::params{});
}


#include "database/asyncdbclient.hpp"
#include "database/asyncdberror.hpp"
#include "database/asyncresultmapper.hpp"
#include "database/asyncsqlbinder.hpp"

drogon::Task<std::optional<jsoncons::json>> DatabaseController::executeQueryCoro(const std::string& query, const pqxx::params& params)
{
    auto asyncClient = Store::getObject<AsyncDbClient>(nullptr);
    if (asyncClient == nullptr || asyncClient->client() == nullptr)
    {
        co_return std::nullopt;
    }
    try
    {
        // Drogon transaction: BEGIN is sent on creation, COMMIT on destruction, ROLLBACK on exception -
        // the asynchronous counterpart of the pqxx::work used by the synchronous path.
        const auto transaction = co_await asyncClient->client()->newTransactionCoro();
        const auto result      = co_await asyncdb::execSqlCoro(*transaction, query, params);
        co_return asyncdb::mapResultToObject(result);
    }
    catch (const std::exception& e)
    {
        Message::CriticalMessage(fmt::format("Exception occurred during async query execution: {}", e.what()));
        if (asyncdb::isBusy(e))
        {
            throw DatabaseBusy();
        }
        co_return std::nullopt;
    }
}

drogon::Task<std::optional<jsoncons::json>> DatabaseController::executeReadQueryCoro(const std::string& query, const pqxx::params& params)
{
    auto asyncClient = Store::getObject<AsyncDbClient>(nullptr);
    if (asyncClient == nullptr || asyncClient->client() == nullptr)
    {
        co_return std::nullopt;
    }
    try
    {
        const auto result = co_await asyncdb::execSqlCoro(*asyncClient->client(), query, params);
        co_return asyncdb::mapResultToObject(result);
    }
    catch (const std::exception& e)
    {
        Message::CriticalMessage(fmt::format("Exception occurred during async read query execution: {}", e.what()));
        if (asyncdb::isBusy(e))
        {
            throw DatabaseBusy();
        }
        co_return std::nullopt;
    }
}

drogon::Task<std::optional<jsoncons::json::array>> DatabaseController::executeSearchQueryCoro(const std::string& query, const pqxx::params& params)
{
    auto asyncClient = Store::getObject<AsyncDbClient>(nullptr);
    if (asyncClient == nullptr || asyncClient->client() == nullptr)
    {
        co_return std::nullopt;
    }
    try
    {
        const auto result = co_await asyncdb::execSqlCoro(*asyncClient->client(), query, params);
        co_return asyncdb::mapResultToArray(result);
    }
    catch (const std::exception& e)
    {
        Message::CriticalMessage(fmt::format("Exception occurred during async search query execution: {}", e.what()));
        if (asyncdb::isBusy(e))
        {
            throw DatabaseBusy();
        }
        co_return std::nullopt;
    }
}

drogon::Task<std::optional<std::string>> DatabaseController::doReadQueryCoro(const std::string& query, const pqxx::params& params)
{
    auto asyncClient = Store::getObject<AsyncDbClient>(nullptr);
    if (asyncClient == nullptr || asyncClient->client() == nullptr)
    {
        co_return std::nullopt;
    }
    try
    {
        const auto result = co_await asyncdb::execSqlCoro(*asyncClient->client(), query, params);
        if (result.empty())
        {
            co_return std::nullopt;
        }
        co_return std::string(result[0][0].c_str());
    }
    catch (const std::exception& e)
    {
        Message::CriticalMessage(fmt::format("Exception occurred during async simple query execution: {}", e.what()));
        if (asyncdb::isBusy(e))
        {
            throw DatabaseBusy();
        }
        co_return std::nullopt;
    }
}

drogon::Task<std::optional<bool>> DatabaseController::checkItemExistsCoro(const std::string& table, const std::string& column, const std::string& value)
{
    auto asyncClient = Store::getObject<AsyncDbClient>(nullptr);
    if (asyncClient == nullptr || asyncClient->client() == nullptr)
    {
        co_return std::nullopt;
    }
    try
    {
        const std::string query = fmt::format("SELECT EXISTS (SELECT 1 FROM {} WHERE {} = $1);", table, column);
        const auto        result = co_await asyncdb::execSqlCoro(*asyncClient->client(), query, pqxx::params{value});
        if (result.empty())
        {
            co_return std::nullopt;
        }
        co_return result[0][0].as<bool>();
    }
    catch (const std::exception& e)
    {
        Message::CriticalMessage(fmt::format("Exception occurred during async exists check: {}", e.what()));
        if (asyncdb::isBusy(e))
        {
            throw DatabaseBusy();
        }
        co_return std::nullopt;
    }
}

drogon::Task<std::optional<jsoncons::json>> DatabaseController::getPasswordHashForUserNameCoro(const std::string& username, const std::string& tablename)
{
    if (tablename != "users" && tablename != "providers")
    {
        Message::ErrorMessage(fmt::format("Unknown client table: {}", tablename));
        co_return std::nullopt;
    }
    const std::string query = fmt::format("SELECT id,password,active FROM {} WHERE username = $1 LIMIT 1;", tablename);
    co_return co_await executeReadQueryCoro(query, pqxx::params{username});
}

drogon::Task<std::optional<std::unordered_set<api::v2::ColumnInfo>>> DatabaseController::getTableSchemaCoro(const std::string& tableName)
{
    auto asyncClient = Store::getObject<AsyncDbClient>(nullptr);
    if (asyncClient == nullptr || asyncClient->client() == nullptr)
    {
        co_return std::nullopt;
    }
    try
    {
        const std::string query = "SELECT column_name, data_type, column_default, is_nullable FROM information_schema.columns WHERE table_name = $1 AND column_name != 'id';";
        const auto        result = co_await asyncdb::execSqlCoro(*asyncClient->client(), query, pqxx::params{tableName});

        std::unordered_set<api::v2::ColumnInfo> schema;
        for (drogon::orm::Result::SizeType row = 0; row < result.size(); ++row)
        {
            api::v2::ColumnInfo column;
            column.Name       = std::string(result[row][0].c_str());
            column.DataType   = std::string(result[row][1].c_str());
            column.Constraint = result[row][2].isNull() ? "None" : std::string(result[row][2].c_str());
            column.isNullable = std::string(result[row][3].c_str()) == "YES";
            schema.insert(column);
        }
        co_return schema;
    }
    catch (const std::exception& e)
    {
        Message::CriticalMessage(fmt::format("Exception occurred during async schema load: {}", e.what()));
        if (asyncdb::isBusy(e))
        {
            throw DatabaseBusy();
        }
        co_return std::nullopt;
    }
}

drogon::Task<std::optional<std::unordered_set<std::string>>> DatabaseController::getAllTablesCoro()
{
    auto asyncClient = Store::getObject<AsyncDbClient>(nullptr);
    if (asyncClient == nullptr || asyncClient->client() == nullptr)
    {
        co_return std::nullopt;
    }
    try
    {
        const auto result = co_await asyncdb::execSqlCoro(*asyncClient->client(), "SELECT table_name FROM information_schema.tables WHERE table_schema = 'public';", pqxx::params{});

        std::unordered_set<std::string> tables;
        for (drogon::orm::Result::SizeType row = 0; row < result.size(); ++row)
        {
            tables.insert(std::string(result[row][0].c_str()));
        }
        co_return tables;
    }
    catch (const std::exception& e)
    {
        Message::CriticalMessage(fmt::format("Exception occurred during async table list: {}", e.what()));
        if (asyncdb::isBusy(e))
        {
            throw DatabaseBusy();
        }
        co_return std::nullopt;
    }
}

drogon::Task<std::optional<jsoncons::json>> DatabaseController::getPermissionsCoro(const std::string& query)
{
    auto asyncClient = Store::getObject<AsyncDbClient>(nullptr);
    if (asyncClient == nullptr || asyncClient->client() == nullptr)
    {
        co_return std::nullopt;
    }
    try
    {
        const auto result = co_await asyncdb::execSqlCoro(*asyncClient->client(), query, pqxx::params{});
        co_return asyncdb::mapResultToObject(result);
    }
    catch (const std::exception& e)
    {
        Message::CriticalMessage(fmt::format("Exception occurred during async permissions query: {}", e.what()));
        if (asyncdb::isBusy(e))
        {
            throw DatabaseBusy();
        }
        co_return std::nullopt;
    }
}
