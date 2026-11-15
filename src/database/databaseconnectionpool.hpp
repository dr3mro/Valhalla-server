#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>

#include "database/blockingpool.hpp"

class Database;
class Configurator;

class DatabaseConnectionPool
{
   public:
    DatabaseConnectionPool();
    DatabaseConnectionPool(const DatabaseConnectionPool&)            = delete;
    DatabaseConnectionPool(DatabaseConnectionPool&&)                 = delete;
    DatabaseConnectionPool& operator=(const DatabaseConnectionPool&) = delete;
    DatabaseConnectionPool& operator=(DatabaseConnectionPool&&)      = delete;
    virtual ~DatabaseConnectionPool()                                = default;

    // Waits up to DB_ACQUIRE_TIMEOUT_MS and throws DatabaseBusy when no connection became free.
    [[nodiscard]] std::shared_ptr<Database> get_connection();
    void                                    return_connection(std::shared_ptr<Database>&& db_ptr);
    void                                    reconnect_all();

   private:
    std::shared_ptr<Database> createDatabaseConnection(const auto& config_);

    std::shared_ptr<Configurator> configurator_;
    BlockingPool<Database>        pool_;
    std::chrono::milliseconds     acquireTimeout_{0};  // DB_ACQUIRE_TIMEOUT_MS, read once at startup
    std::atomic<std::int64_t>     lastSlowWaitWarningMs_{0};  // rate limits the slow-wait warning

    static constexpr std::int64_t SLOW_WAIT_WARNING_MS = 100;

    static constexpr std::uint16_t TIMEOUT     = 2;
    static constexpr unsigned int  MAX_RETRIES = 5;
};
