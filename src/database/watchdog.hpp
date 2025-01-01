#pragma once

#include <atomic>
#include <chrono>
#include <pqxx/pqxx>
#include <thread>

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
    std::atomic<bool>    should_monitor{true};
    std::chrono::seconds check_interval{1};
    std::thread          monitor_thread;
};