#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <fmt/format.h>
#include <trantor/utils/Logger.h>

#include <cstdint>
#include <jsoncons/basic_json.hpp>
#include <jsoncons/json.hpp>
#include <memory>
#include <optional>
#include <pqxx/pqxx>
#include <string>
#include <unordered_set>

#include "database/connectionguard.hpp"
#include "utils/global/types.hpp"

class Database
{
   public:
    Database(const Database &)            = delete;
    Database(Database &&)                 = delete;
    Database &operator=(const Database &) = delete;
    Database &operator=(Database &&)      = delete;
    explicit Database(std::shared_ptr<pqxx::connection> &&conn);
    virtual ~Database();

    bool checkExists(const std::string &table, const std::string &column, const std::string &value);
    bool check_connection();
    bool reconnect();

    template <typename jsonType, typename TransactionType>
    std::optional<jsonType> executeQuery(const std::string &query);

    template <typename T>
    std::optional<T> doSimpleQuery(const std::string &query);

    std::optional<std::unordered_set<api::v2::ColumnInfo>> getTableSchema(const std::string &tableName);
    std::optional<std::unordered_set<std::string>>         getAllTables();

   private:
    void waitForConnection();

    std::shared_ptr<pqxx::connection> connection;
    std::string                       connection_info;  // Store connection parameters
    std::unique_ptr<ConnectionGuard>  connectionGuard;

    static const std::uint16_t TEXT    = 1043;
    static const std::uint16_t INTEGER = 23;
    static const std::uint16_t BOOLEAN = 16;
    static const std::uint16_t JSON    = 114;
    static const std::uint16_t JSONB   = 3802;
};

#endif  // DATABASE_HPP
