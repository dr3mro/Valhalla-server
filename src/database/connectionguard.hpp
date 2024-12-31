#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <pqxx/pqxx>

class ConnectionGuard
{
   public:
    ConnectionGuard()                                   = delete;
    ConnectionGuard(const ConnectionGuard &)            = delete;
    ConnectionGuard(ConnectionGuard &&)                 = delete;
    ConnectionGuard &operator=(const ConnectionGuard &) = delete;
    ConnectionGuard &operator=(ConnectionGuard &&)      = delete;

    explicit ConnectionGuard(std::shared_ptr<pqxx::connection> &conn);
    ~ConnectionGuard();

    void setInUse(bool state);
    void setIsRead(bool state);

    bool getInUse();
    bool getIsReady();

    void waitForConnection();

   private:
    std::shared_ptr<pqxx::connection> connection;

    std::atomic<bool> is_ready;
    std::atomic<bool> in_use;

    static const std::uint64_t CONNECTION_READY_TIMEOUT = 50;
};
