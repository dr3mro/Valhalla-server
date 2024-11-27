#pragma once
#include <jsoncons/json.hpp>

#include "database/database.hpp"
#include "database/databaseconnectionpool.hpp"
#include "utils/message/message.hpp"

class DatabaseController
{
   public:
    DatabaseController();
    virtual ~DatabaseController() = default;

    std::optional<jsoncons::json>        executeQuery(const std::string &query);
    std::optional<jsoncons::json>        executeReadQuery(const std::string &query);
    std::optional<jsoncons::json::array> executeSearchQuery(const std::string &query);
    std::optional<std::string>           doReadQuery(const std::string &query);
    std::optional<bool>                  checkItemExists(const std::string &table, const std::string &column, const std::string &value);
    std::optional<jsoncons::json>        getPasswordHashForUserName(const std::string &username, const std::string &tablename);
    std::optional<uint64_t> findIfUserID(const std::string &username, const std::string &tablename);  // check if user found and return 0 if not
    std::optional<std::unordered_set<api::v2::ColumnInfo>> getTableSchema(const std::string &tableName);
    std::optional<std::unordered_set<std::string>>         getAllTables();

   private:
    std::shared_ptr<DatabaseConnectionPool> databaseConnectionPool;

    template <typename R, typename F, typename... Args>
    std::optional<R> executer(const F &f, Args &&...args)
    {
        std::shared_ptr<Database> db = nullptr;
        try
        {
            db                       = databaseConnectionPool->get_connection();
            std::optional<R> results = std::invoke(f, db.get(), std::forward<Args>(args)...);
            //(db.get()->*f)(std::forward<Args>(args)...);
            databaseConnectionPool->return_connection(std::move(db));
            if (results)
            {
                return results.value();
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
