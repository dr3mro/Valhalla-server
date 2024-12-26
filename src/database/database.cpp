#include "database.hpp"

#include <fmt/core.h>

#include <cstdlib>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include "utils/global/types.hpp"
#include "utils/message/message.hpp"

Database::Database(std::shared_ptr<pqxx::connection> &&conn) : connection(std::move(conn))
{
    try
    {
        if (connection != nullptr)
        {
            pqxx::nontransaction ntxn(*connection);

            ntxn.exec("SELECT 1");

            connection_info = connection->connection_string();
        }
    }
    catch (const std::exception &e)
    {
        Message::CriticalMessage(fmt::format("Exception caught during database connection: {}", e.what()));
    }
}
Database::~Database() { Message::InfoMessage(fmt::format("Database connection {} is now exterminated.", static_cast<const void *>(this))); }

bool Database::checkExists(const std::string &table, const std::string &column, const std::string &value)
{
    try
    {
        std::lock_guard<std::mutex> guard(connection_mutex);
        pqxx::nontransaction        txn(*connection);
        pqxx::result                result = txn.exec(fmt::format("SELECT EXISTS (SELECT 1 FROM {} WHERE {} = '{}');", table, column, value));
        return result[0][0].as<bool>();
    }
    catch (const std::exception &e)
    {
        Message::CriticalMessage(fmt::format("Error executing query: {}", e.what()));
        return false;
    }
}

bool Database::check_connection()
{
    std::lock_guard<std::mutex> guard(connection_mutex);
    if (connection == nullptr)
    {
        return false;
    }
    try
    {
        pqxx::nontransaction ntxn(*connection);
        ntxn.exec("SELECT 1");
    }
    catch (const std::exception &e)
    {
        return false;
    }

    return connection->is_open();
}

bool Database::reconnect()
{
    try
    {
        {
            std::lock_guard<std::mutex> guard(connection_mutex);
            connection = std::make_shared<pqxx::connection>(connection_info);
        }

        return check_connection();
    }
    catch (const std::exception &e)
    {
        Message::CriticalMessage(fmt::format("Failed to reconnect to database,{}", e.what()));
        return false;
    }
}

std::optional<std::unordered_set<api::v2::ColumnInfo>> Database::getTableSchema(const std::string &tableName)
{
    try
    {
        pqxx::result result;

        {
            std::lock_guard<std::mutex> guard(connection_mutex);
            pqxx::nontransaction        ntxn(*connection);
            std::string                 query = fmt::format(
                "SELECT column_name, data_type, column_default, is_nullable FROM "
                                "information_schema.columns WHERE table_name = '{}' AND column_name != 'id';",
                tableName);

            result = ntxn.exec(query);
        }

        std::unordered_set<api::v2::ColumnInfo> schema;

        for (const auto &row : result)
        {
            api::v2::ColumnInfo column;
            column.Name       = row["column_name"].as<std::string>();
            column.DataType   = row["data_type"].as<std::string>();
            column.Constraint = row["column_default"].is_null() ? "None" : row["column_default"].as<std::string>();
            column.isNullable = row["is_nullable"].as<std::string>() == "YES";
            schema.insert(column);
        }
        return schema;
    }
    catch (const std::exception &e)
    {
        Message::CriticalMessage(fmt::format("Error executing query: {}", e.what()));
        return std::nullopt;
    }
}

std::optional<std::unordered_set<std::string>> Database::getAllTables()
{
    try
    {
        pqxx::result result;

        {
            std::lock_guard<std::mutex> guard(connection_mutex);
            pqxx::work                  txn(*connection);
            result = txn.exec("SELECT table_name FROM information_schema.tables WHERE table_schema = 'public';");
        }

        std::unordered_set<std::string> tables;

        for (const auto &row : result)
        {
            tables.insert(row["table_name"].as<std::string>());
        }
        return tables;
    }
    catch (const std::exception &e)
    {
        Message::CriticalMessage(fmt::format("Error executing query: {}", e.what()));
        return std::nullopt;
    }
}
