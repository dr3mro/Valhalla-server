#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <fmt/format.h>
#include <trantor/utils/Logger.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <jsoncons/basic_json.hpp>
#include <jsoncons/json.hpp>
#include <memory>
#include <optional>
#include <pqxx/pqxx>  // Include the libpqxx header for PostgreSQL
#include <string>
#include <thread>
#include <unordered_set>

#include "utils/global/types.hpp"
#include "utils/message/message.hpp"

class Database
{
   public:
    using MonitorConfig = struct MonitorConfig
    {
        std::chrono::seconds check_interval{10};     // NOLINT
        std::chrono::seconds max_backoff{300};       // NOLINT // 5 minutes
        int                  max_retry_attempts{5};  // NOLINT
    };

    Database(const Database &)            = delete;
    Database(Database &&)                 = delete;
    Database &operator=(const Database &) = delete;
    Database &operator=(Database &&)      = delete;
    explicit Database(std::shared_ptr<pqxx::connection> conn);
    virtual ~Database() { stopConnectionMonitor(); }

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
    void startConnectionMonitor();
    void stopConnectionMonitor();

    std::thread       monitor_thread;
    std::atomic<bool> should_monitor{false};
    std::string       connection_info;  // Store connection parameters

    std::shared_ptr<pqxx::connection> connection;
    MonitorConfig                     config;

    static const std::uint16_t TEXT    = 1043;
    static const std::uint16_t INTEGER = 23;
    static const std::uint16_t BOOLEAN = 16;
    static const std::uint16_t JSON    = 114;
    static const std::uint16_t JSONB   = 3802;
};

#endif  // DATABASE_HPP
