#include "databasecontroller.hpp"

#include <fmt/core.h>

#include <cstdint>
#include <jsoncons/basic_json.hpp>
#include <optional>
#include <string>
#include <unordered_set>

#include "database/database.hpp"
#include "database/databaseconnectionpool.hpp"
#include "database/watchdog.hpp"
#include "store/store.hpp"
#include "utils/global/types.hpp"

DatabaseController::DatabaseController() : databaseConnectionPool_(Store::getObject<DatabaseConnectionPool>()), watchDog_(Store::getObject<WatchDog>()) {}
std::optional<jsoncons::json> DatabaseController::executeQuery(const std::string &query, bool &isSqlInjection)
{
    return executer<jsoncons::json>(&Database::executeQuery<jsoncons::json, pqxx::work>, query, isSqlInjection);
}

std::optional<jsoncons::json> DatabaseController::executeReadQuery(const std::string &query, bool &isSqlInjection)
{
    return executer<jsoncons::json>(&Database::executeQuery<jsoncons::json, pqxx::nontransaction>, query, isSqlInjection);
}

std::optional<jsoncons::json ::array> DatabaseController::executeSearchQuery(const std::string &query, bool &isSqlInjection)
{
    return executer<jsoncons::json::array>(&Database::executeQuery<jsoncons::json::array, pqxx::nontransaction>, query, isSqlInjection);
}

std::optional<std::string> DatabaseController::doReadQuery(const std::string &query, bool &isSqlInjection)
{
    return executer<std::string>(&Database::doSimpleQuery<std::string>, query, isSqlInjection);
}

std::optional<bool> DatabaseController::checkItemExists(const std::string &table, const std::string &column, const std::string &value, bool &isSqlInjection)
{
    return executer<bool>(&Database::checkExists, table, column, value, isSqlInjection);
}

std::optional<jsoncons::json> DatabaseController::getPasswordHashForUserName(const std::string &username, const std::string &tablename, bool &isSqlInjection)
{
    std::string query = fmt::format("SELECT id,password,active FROM {} WHERE username = '{}' LIMIT 1;", tablename, username);
    return executer<jsoncons::json>(&Database::executeQuery<jsoncons::json, pqxx::nontransaction>, query, isSqlInjection);
}

std::optional<uint64_t> DatabaseController::findIfUserID(const std::string &username, const std::string &tablename, bool &isSqlInjection)
{
    std::string query = fmt::format("SELECT id FROM {} WHERE id = '{}' LIMIT 1;", tablename, username);
    return executer<uint64_t>(&Database::doSimpleQuery<uint64_t>, query, isSqlInjection);
}

std::optional<std::unordered_set<api::v2::ColumnInfo>> DatabaseController::getTableSchema(const std::string &tableName)
{
    return executer<std::unordered_set<api::v2::ColumnInfo>>(&Database::getTableSchema, tableName);
}

std::optional<std::unordered_set<std::string>> DatabaseController::getAllTables() { return executer<std::unordered_set<std::string>>(&Database::getAllTables); }

std::optional<jsoncons::json> DatabaseController::getPermissions(const std::string &query, bool &isSqlInjection)
{
    return executer<jsoncons::json>(&Database::executeQuery<jsoncons::json, pqxx::nontransaction>, query, isSqlInjection);
}
