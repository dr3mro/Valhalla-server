#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <pqxx/pqxx>
#include <thread>

class DatabaseConnectionPool;
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
    std::atomic<bool>                       should_monitor{true};
    std::shared_ptr<DatabaseConnectionPool> databaseConnectionPool;
    std::chrono::seconds                    check_interval{1};
    std::thread                             monitor_thread;
};