#include "databasecontroller.hpp"

#include <fmt/core.h>

#include <jsoncons/basic_json.hpp>

#include "store/store.hpp"
#include "utils/message/message.hpp"

DatabaseController::DatabaseController()
{
    try
    {
        databaseConnectionPool = Store::getObject<DatabaseConnectionPool>();
    }
    catch (const std::exception &e)
    {
        Message::ErrorMessage("Exception in DatabaseController constructor.");
        Message::CriticalMessage(e.what());
        exit(EXIT_FAILURE);
    }
}

std::optional<jsoncons::json> DatabaseController::executeQuery(const std::string &query)
{
    return executer<jsoncons::json>(&Database::executeQuery<jsoncons::json, pqxx::work>, query);
}

std::optional<jsoncons::json> DatabaseController::executeReadQuery(const std::string &query)
{
    return executer<jsoncons::json>(&Database::executeQuery<jsoncons::json, pqxx::nontransaction>, query);
}

std::optional<jsoncons::json ::array> DatabaseController::executeSearchQuery(const std::string &query)
{
    return executer<jsoncons::json ::array>(&Database::executeQuery<jsoncons::json ::array, pqxx::nontransaction>, query);
}

std::optional<std::string> DatabaseController::doReadQuery(const std::string &query)
{
    return executer<std::string>(&Database::doSimpleQuery<std::string>, query);
}

std::optional<bool> DatabaseController::checkItemExists(const std::string &table, const std::string &column, const std::string &value)
{
    return executer<bool>(&Database::checkExists, table, column, value).value();
}

std::optional<jsoncons::json> DatabaseController::getPasswordHashForUserName(const std::string &username, const std::string &tablename)
{
    std::string query = fmt::format("SELECT id,password,active FROM {} WHERE username = '{}' LIMIT 1;", tablename, username);
    return executer<jsoncons::json>(&Database::executeQuery<jsoncons::json, pqxx::nontransaction>, query);
}

std::optional<uint64_t> DatabaseController::findIfUserID(const std::string &username, const std::string &tablename)
{
    return executer<uint64_t>(&Database::doSimpleQuery<uint64_t>, fmt::format("SELECT id FROM {} WHERE id = '{}' LIMIT 1;", tablename, username));
}

std::optional<std::unordered_set<api::v2::ColumnInfo>> DatabaseController::getTableSchema(const std::string &tableName)
{
    return executer<std::unordered_set<api::v2::ColumnInfo>>(&Database::getTableSchema, tableName);
}

std::optional<std::unordered_set<std::string>> DatabaseController::getAllTables() { return executer<std::unordered_set<std::string>>(&Database::getAllTables); }

// [ ] make entity return the query for getpermission and use T template here.
