#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <fmt/format.h>

#include <jsoncons/json.hpp>
#include <pqxx/pqxx>  // Include the libpqxx header for PostgreSQL
#include <string>

#include "utils/global/types.hpp"
#include "utils/message/message.hpp"

class Database
{
   public:
    explicit Database(std::shared_ptr<pqxx::connection> conn);
    virtual ~Database() = default;

    bool isConnected();
    bool checkExists(const std::string &table, const std::string &column, const std::string &value);

    template <typename jsonType, typename TransactionType>
    std::optional<jsonType> executeQuery(const std::string &query)
    {
        try
        {
            TransactionType txn(*connection);
            pqxx::result    res = txn.exec(query);

            if constexpr (std::is_same_v<TransactionType, pqxx::work>)
            {
                txn.commit();
            }

            jsonType       reply;
            jsoncons::json object;

            for (const auto &row : res)
            {
                for (const auto &field : row)
                {
                    std::string field_name = field.name();
                    int         field_type = field.type();

                    if (field.is_null())
                    {
                        object[field_name] = nullptr;
                        LOG_WARN << "Field " << field_name << " is null";
                    }
                    else
                    {
                        switch (field_type)
                        {
                            case 1043:  // TEXT or VARCHAR
                                object[field_name] = field.as<std::string>();
                                break;

                            case 23:  // INTEGER
                                object[field_name] = field.as<int>();
                                break;

                            case 16:  // BOOLEAN
                                object[field_name] = field.as<bool>();
                                break;

                            case 114:   // JSON
                            case 3802:  // JSONB
                                object[field_name] = jsoncons::json::parse(field.as<std::string>());
                                break;

                            default:                                 // Handle unknown or unhandled types
                                object[field_name] = field.c_str();  // Default to string representation
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
    std::optional<T> doSimpleQuery(const std::string &query)
    {
        try
        {
            pqxx::nontransaction txn(*connection);
            pqxx::result         result = txn.exec(query);
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

    std::optional<std::unordered_set<api::v2::ColumnInfo>> getTableSchema(const std::string &tableName);
    std::optional<std::unordered_set<std::string>>         getAllTables();

   private:
    std::shared_ptr<pqxx::connection> connection;
};

#endif  // DATABASE_HPP
