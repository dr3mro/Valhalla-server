#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <pqxx/pqxx>
#include <thread>

#include "database/databaseconnectionpool.hpp"
#include "store/store.hpp"

class Database;
class WatchDog
{
   public:
    WatchDog();
    WatchDog(const WatchDog &)            = delete;
    WatchDog(WatchDog &&)                 = delete;
    WatchDog &operator=(const WatchDog &) = delete;
    WatchDog &operator=(WatchDog &&)      = delete;
    virtual ~WatchDog();

   private:
    void reconnect_all();

    std::shared_ptr<DatabaseConnectionPool> databaseConnectionPool = Store::getObject<DatabaseConnectionPool>();
    std::chrono::seconds                    check_interval{1};
    std::atomic<bool>                       should_monitor{true};
    std::thread                             monitor_thread;
};