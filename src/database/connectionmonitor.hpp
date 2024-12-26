#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <pqxx/pqxx>
#include <thread>

#include "database/databaseconnectionpool.hpp"
#include "store/store.hpp"

class Database;
class ConnectionMonitor
{
    using MonitorConfig = struct MonitorConfig
    {
        std::chrono::seconds check_interval{1};  // NOLINT
        std::chrono::seconds max_backoff{32};    // NOLINT
    };

   public:
    ConnectionMonitor();
    ConnectionMonitor(const ConnectionMonitor &)            = delete;
    ConnectionMonitor(ConnectionMonitor &&)                 = delete;
    ConnectionMonitor &operator=(const ConnectionMonitor &) = delete;
    ConnectionMonitor &operator=(ConnectionMonitor &&)      = delete;
    virtual ~ConnectionMonitor();

   private:
    std::shared_ptr<DatabaseConnectionPool> databaseConnectionPool = Store::getObject<DatabaseConnectionPool>();
    MonitorConfig                           config;
    std::thread                             monitor_thread;
    std::atomic<bool>                       should_monitor{true};
};