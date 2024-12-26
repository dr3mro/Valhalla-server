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
   public:
    ConnectionMonitor();
    ConnectionMonitor(const ConnectionMonitor &)            = delete;
    ConnectionMonitor(ConnectionMonitor &&)                 = delete;
    ConnectionMonitor &operator=(const ConnectionMonitor &) = delete;
    ConnectionMonitor &operator=(ConnectionMonitor &&)      = delete;
    virtual ~ConnectionMonitor();

   private:
    std::shared_ptr<DatabaseConnectionPool> databaseConnectionPool = Store::getObject<DatabaseConnectionPool>();
    std::chrono::seconds                    check_interval{1};
    std::atomic<bool>                       should_monitor{true};
    std::thread                             monitor_thread;
};