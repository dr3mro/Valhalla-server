#include "database.hpp"

#include <fmt/core.h>
#include <trantor/utils/Logger.h>

#include <cstdlib>
#include <exception>
#include <jsoncons/basic_json.hpp>
#include <jsoncons/json.hpp>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
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
        pqxx::nontransaction txn(*connection);
        pqxx::result         result = txn.exec(fmt::format("SELECT EXISTS (SELECT 1 FROM {} WHERE {} = '{}');", table, column, value));
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
        connection = std::make_shared<pqxx::connection>(connection_info);
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
            pqxx::nontransaction ntxn(*connection);
            std::string          query = fmt::format(
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

        pqxx::work txn(*connection);
        result = txn.exec("SELECT table_name FROM information_schema.tables WHERE table_schema = 'public';");

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

template <typename jsonType, typename TransactionType>
std::optional<jsonType> Database::executeQuery(const std::string &query)
{
    static_assert(std::is_same_v<jsonType, jsoncons::json> || std::is_same_v<jsonType, jsoncons::json::array>, "Unsupported jsonType specialization");

    try
    {
        pqxx::result results;

        TransactionType txn(*connection);

        results = txn.exec(query);

        if constexpr (std::is_same_v<TransactionType, pqxx::work>)
        {
            txn.commit();
        }

        jsonType       reply;
        jsoncons::json object;

        for (const auto &row : results)
        {
            for (const auto &field : row)
            {
                std::string  field_name = field.name();
                unsigned int field_type = field.type();

                if (field.is_null())
                {
                    object[field_name] = nullptr;
                    LOG_WARN << "Field " << field_name << " is null";
                }
                else
                {
                    switch (field_type)
                    {
                        case TEXT:  // TEXT or VARCHAR
                            object[field_name] = field.as<std::string>();
                            break;

                        case INTEGER:  // INTEGER
                            object[field_name] = field.as<int>();
                            break;

                        case BOOLEAN:  // BOOLEAN
                            object[field_name] = field.as<bool>();
                            break;

                        case JSON:   // JSON
                        case JSONB:  // JSONB
                            object[field_name] = jsoncons::json::parse(field.as<std::string>());
                            break;

                        default:                                 // Handle unknown or unhandled types
                            object[field_name] = field.c_str();  // Default to string
                                                                 // representation
                            break;
                    }
                }
            }
            if constexpr (std::is_same_v<jsonType, jsoncons::json::array>)
            {
                reply.push_back(object);
            }
            else
            {
                return object;
            }
        }
        return reply;
    }
    catch (const std::exception &e)
    {
        Message::ErrorMessage("Error executing query:");
        Message::InfoMessage(query);
        Message::CriticalMessage(e.what());
        return std::nullopt;
        // throw;  // Rethrow the exception to indicate failure
    }
    return std::nullopt;
}

template <typename T>
std::optional<T> Database::doSimpleQuery(const std::string &query)
{
    try
    {
        pqxx::result result;

        pqxx::nontransaction txn(*connection);

        result = txn.exec(query);

        return result.empty() ? std::nullopt : result[0][0].as<std::optional<T>>();
    }
    catch (const std::exception &e)
    {
        Message::ErrorMessage("Error executing query:");
        Message::InfoMessage(query);
        Message::CriticalMessage(e.what());
        return std::nullopt;
        // throw;  // Rethrow the exception to indicate failure
    }
}

template std::optional<uint64_t>              Database::doSimpleQuery<uint64_t>(const std::string &);
template std::optional<std::string>           Database::doSimpleQuery<std::string>(const std::string &);
template std::optional<jsoncons::json>        Database::executeQuery<jsoncons::json, pqxx::nontransaction>(const std::string &);
template std::optional<jsoncons::json>        Database::executeQuery<jsoncons::json, pqxx::work>(const std::string &);
template std::optional<jsoncons::json::array> Database::executeQuery<jsoncons::json::array, pqxx::nontransaction>(const std::string &);
