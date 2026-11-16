#pragma once

#include "database/dbprobe.hpp"
#include <fmt/core.h>
#include <fmt/format.h>

#include <atomic>
#include <cstdint>
#include <exception>
#include <jsoncons/basic_json.hpp>
#include <jsoncons/json.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <pqxx/pqxx>
#include <string>
#include <unordered_set>
#include <utility>

#include "configurator/configurator.hpp"
#include "database/databasebusy.hpp"
#include "database/asyncdbclient.hpp"
#include "database/asyncresultmapper.hpp"
#include "database/asyncsqlbinder.hpp"
#include "drogon/utils/coroutine.h"
#include "database/databasehandler.hpp"
#include "store/store.hpp"
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
    DatabaseController(const DatabaseController&)            = delete;
    DatabaseController(DatabaseController&&)                 = delete;
    DatabaseController& operator=(const DatabaseController&) = delete;
    DatabaseController& operator=(DatabaseController&&)      = delete;
    virtual ~DatabaseController()                            = default;

    virtual std::optional<jsoncons::json>        executeQuery(const std::string& query, const pqxx::params& params = {});
    virtual std::optional<jsoncons::json>        executeReadQuery(const std::string& query, const pqxx::params& params = {});
    virtual std::optional<jsoncons::json::array> executeSearchQuery(const std::string& query, const pqxx::params& params = {});
    virtual std::optional<std::string>           doReadQuery(const std::string& query, const pqxx::params& params = {});
    virtual std::optional<bool>                  checkItemExists(const std::string& table, const std::string& column, const std::string& value);
    virtual std::optional<jsoncons::json>        getPasswordHashForUserName(const std::string& username, const std::string& tablename);
    virtual std::optional<std::unordered_set<api::v2::ColumnInfo>> getTableSchema(const std::string& tableName);
    virtual std::optional<std::unordered_set<std::string>>         getAllTables();
    virtual std::optional<jsoncons::json>                          getPermissions(const std::string& query);

    // Asynchronous (coroutine) entry points used when DB_ASYNC=true. They run on
    // the Drogon IO loop without a worker hand-off.
    virtual drogon::Task<std::optional<jsoncons::json>>        executeQueryCoro(const std::string& query, const pqxx::params& params = {});
    virtual drogon::Task<std::optional<jsoncons::json>>        executeReadQueryCoro(const std::string& query, const pqxx::params& params = {});
    virtual drogon::Task<std::optional<jsoncons::json::array>> executeSearchQueryCoro(const std::string& query, const pqxx::params& params = {});
    virtual drogon::Task<std::optional<std::string>>           doReadQueryCoro(const std::string& query, const pqxx::params& params = {});
    virtual drogon::Task<std::optional<bool>>                  checkItemExistsCoro(const std::string& table, const std::string& column, const std::string& value);
    virtual drogon::Task<std::optional<jsoncons::json>>        getPasswordHashForUserNameCoro(const std::string& username, const std::string& tablename);
    virtual drogon::Task<std::optional<std::unordered_set<api::v2::ColumnInfo>>> getTableSchemaCoro(const std::string& tableName);
    virtual drogon::Task<std::optional<std::unordered_set<std::string>>>         getAllTablesCoro();
    virtual drogon::Task<std::optional<jsoncons::json>>        getPermissionsCoro(const std::string& query);

   private:
    std::atomic<bool>                       backendsReady_{false};
    std::shared_ptr<DatabaseConnectionPool> databaseConnectionPool_;
    std::shared_ptr<WatchDog>               watchDog_;
    std::shared_ptr<Configurator::Testing>  testing_config_ = std::make_shared<Configurator::Testing>();
    void                                    ensureBackends();  // lazily creates the connection pool and watchdog

    template <typename Result, typename Func, typename... Args>
    std::optional<Result> executer(const Func& func, Args&&... args)
    {
        DbProbe::check();  // on the IO-thread fast path this throws WouldBlock: the request moves to the DB executor
        try
        {
            ensureBackends();

            // Hermetic test mode: no backends were created, so report the
            // database as unavailable without constructing a handler (which
            // would create a pool via Store and exit() on init failure).
            if (!databaseConnectionPool_)
            {
                return std::nullopt;
            }

            std::unique_ptr<DatabaseHanndler> dbHandler = std::make_unique<DatabaseHanndler>(databaseConnectionPool_);
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
        catch (const DatabaseBusy&)
        {
            throw;  // the pool is exhausted: callers answer 503 instead of treating it as a failed query
        }
        catch (const std::exception& e)
        {
            Message::CriticalMessage(fmt::format("Exception occurred during query execution: {}", e.what()));
            return std::nullopt;
        }
        catch (...)
        {
            Message::CriticalMessage("Unknown exception occurred during query execution.");
            return std::nullopt;
        }
    }
};
