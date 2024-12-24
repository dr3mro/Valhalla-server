#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <pqxx/pqxx>
#include <thread>
#include <utility>

class Database;
class ConnectionMonitor
{
    using MonitorConfig = struct MonitorConfig
    {
        std::chrono::seconds check_interval{10};      // NOLINT
        std::chrono::seconds max_backoff{300};        // NOLINT // 5 minutes
        int                  max_retry_attempts{10};  // NOLINT
    };

   public:
    explicit ConnectionMonitor(std::shared_ptr<Database> db_ptr) : db_ptr(std::move(db_ptr)) {}
    ConnectionMonitor()                                     = delete;
    ConnectionMonitor(const ConnectionMonitor &)            = delete;
    ConnectionMonitor(ConnectionMonitor &&)                 = delete;
    ConnectionMonitor &operator=(const ConnectionMonitor &) = delete;
    ConnectionMonitor &operator=(ConnectionMonitor &&)      = delete;
    virtual ~ConnectionMonitor()                            = default;

    void start();
    void stop();

   private:
    std::shared_ptr<Database> db_ptr;
    MonitorConfig             config;
    std::thread               monitor_thread;
    std::atomic<bool>         should_monitor{false};
};