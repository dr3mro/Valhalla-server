#pragma once
#include <fmt/core.h>
#include <fmt/format.h>

#include <cstdint>
#include <exception>
#include <jsoncons/basic_json.hpp>
#include <jsoncons/json.hpp>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include "database/databasehandler.hpp"
#include "utils/global/types.hpp"
#include "utils/message/message.hpp"
class Case;
class Service;
class Appointment;
class Database;
class WatchDog;
class DatabaseController
{
   public:
    DatabaseController();
    DatabaseController(const DatabaseController &)            = default;
    DatabaseController(DatabaseController &&)                 = delete;
    DatabaseController &operator=(const DatabaseController &) = default;
    DatabaseController &operator=(DatabaseController &&)      = delete;
    virtual ~DatabaseController()                             = default;

    std::optional<jsoncons::json>        executeQuery(const std::string &query, bool &isSqlInjection);
    std::optional<jsoncons::json>        executeReadQuery(const std::string &query, bool &isSqlInjection);
    std::optional<jsoncons::json::array> executeSearchQuery(const std::string &query, bool &isSqlInjection);
    std::optional<std::string>           doReadQuery(const std::string &query, bool &isSqlInjection);
    std::optional<bool>                  checkItemExists(const std::string &table, const std::string &column, const std::string &value, bool &isSqlInjection);
    std::optional<jsoncons::json>        getPasswordHashForUserName(const std::string &username, const std::string &tablename, bool &isSqlInjection);
    std::optional<uint64_t>              findIfUserID(
                     const std::string &username, const std::string &tablename, bool &isSqlInjection);  // check if user found and return 0 if not
    std::optional<std::unordered_set<api::v2::ColumnInfo>> getTableSchema(const std::string &tableName);
    std::optional<std::unordered_set<std::string>>         getAllTables();
    std::optional<jsoncons::json>                          getPermissions(const std::string &query, bool &isSqlInjection);

   private:
    std::shared_ptr<DatabaseConnectionPool> databaseConnectionPool_;
    std::shared_ptr<WatchDog>               watchDog_;

    template <typename Result, typename Func, typename... Args>
    std::optional<Result> executer(const Func &func, Args &&...args)
    {
        try
        {
            std::unique_ptr<DatabaseHanndler> dbHandler = std::make_unique<DatabaseHanndler>();
            std::shared_ptr<Database>         db_ptr    = dbHandler->get_connection();

            if (db_ptr == nullptr)
            {
                return std::nullopt;
            }

            std::optional<Result> results = std::invoke(func, db_ptr.get(), std::forward<Args>(args)...);

            if (results.has_value())
            {
                return results;
            }

            return std::nullopt;
        }
        catch (const std::exception &e)
        {
            Message::CriticalMessage(fmt::format("Exception occurred during query execution.", e.what()));
            return std::nullopt;
        }
        catch (...)
        {
            Message::CriticalMessage("Unknown exception occurred during query execution.");
            return std::nullopt;
        }
    }
};
