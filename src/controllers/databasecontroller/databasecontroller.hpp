#pragma once
#include <cstdint>
#include <exception>
#include <jsoncons/basic_json.hpp>
#include <jsoncons/json.hpp>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include "database/database.hpp"
#include "database/databaseconnectionpool.hpp"
#include "utils/global/types.hpp"
#include "utils/message/message.hpp"
class Case;
class Service;
class Appointment;

class DatabaseController
{
   public:
    DatabaseController();
    virtual ~DatabaseController() = default;

    std::optional<jsoncons::json>                          executeQuery(const std::string &query);
    std::optional<jsoncons::json>                          executeReadQuery(const std::string &query);
    std::optional<jsoncons::json::array>                   executeSearchQuery(const std::string &query);
    std::optional<std::string>                             doReadQuery(const std::string &query);
    std::optional<bool>                                    checkItemExists(const std::string &table, const std::string &column, const std::string &value);
    std::optional<jsoncons::json>                          getPasswordHashForUserName(const std::string &username, const std::string &tablename);
    std::optional<uint64_t>                                findIfUserID(const std::string &username,
                                       const std::string                                  &tablename);  // check if user found and return 0 if not
    std::optional<std::unordered_set<api::v2::ColumnInfo>> getTableSchema(const std::string &tableName);
    std::optional<std::unordered_set<std::string>>         getAllTables();

    std::optional<jsoncons::json> getPermissions(const std::string &query)
    {
        return executer<jsoncons::json>(&Database::executeQuery<jsoncons::json, pqxx::nontransaction>, query);
    }

   private:
    std::shared_ptr<DatabaseConnectionPool> databaseConnectionPool;

    template <typename Result, typename Func, typename... Args>
    std::optional<Result> executer(const Func &func, Args &&...args)
    {
        try
        {
            std::shared_ptr<Database> db_ptr  = databaseConnectionPool->get_connection();
            std::optional<Result>     results = std::invoke(func, db_ptr.get(), std::forward<Args>(args)...);

            databaseConnectionPool->return_connection(std::move(db_ptr));
            if (results.has_value())
            {
                return results;
            }
        }
        catch (const std::exception &e)
        {
            Message::ErrorMessage("Exception occurred during query execution.");
            Message::CriticalMessage(e.what());
        }
        catch (...)
        {
            Message::CriticalMessage("Unknown exception occurred during query execution.");
        }
        return std::nullopt;
    }
};
