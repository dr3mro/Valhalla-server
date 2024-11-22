#include "databasecontroller.hpp"

#include <fmt/core.h>

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

std::optional<json> DatabaseController::executeQuery(const std::string &query)
{
    return executer<json>(&Database::executeQuery<json, pqxx::work>, query);
}

std::optional<json> DatabaseController::executeReadQuery(const std::string &query)
{
    return executer<json>(&Database::executeQuery<json, pqxx::nontransaction>, query);
}

std::optional<json::array> DatabaseController::executeSearchQuery(const std::string &query)
{
    return executer<json::array>(&Database::executeQuery<json::array, pqxx::nontransaction>, query);
}

std::optional<std::string> DatabaseController::doReadQuery(const std::string &query)
{
    return executer<std::string>(&Database::doSimpleQuery<std::string>, query);
}

std::optional<bool> DatabaseController::checkItemExists(const std::string &table, const std::string &column, const std::string &value)
{
    return executer<bool>(&Database::checkExists, table, column, value).value();
}

std::optional<std::string> DatabaseController::getPasswordHashForUserID(const uint64_t &user_id, const std::string &tablename)
{
    return executer<std::string>(&Database::doSimpleQuery<std::string>,
                                 fmt::format("SELECT password FROM {} WHERE id = '{}' LIMIT 1;", tablename, user_id));
}

std::optional<uint64_t> DatabaseController::findIfUserID(const std::string &username, const std::string &tablename)
{
    return executer<uint64_t>(&Database::doSimpleQuery<uint64_t>,
                              fmt::format("SELECT id FROM {} WHERE username = '{}' LIMIT 1;", tablename, username));
}

std::optional<std::vector<Database::ColumnInfo>> DatabaseController::getTableSchema(const std::string &tableName)
{
    return executer<std::vector<Database::ColumnInfo>>(&Database::getTableSchema, tableName);
}

std::optional<std::vector<std::string>> DatabaseController::getAllTables() { return executer<std::vector<std::string>>(&Database::getAllTables); }