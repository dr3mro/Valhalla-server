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
    static std::mutex backendInitMutex;
    std::scoped_lock  lock(backendInitMutex);
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

