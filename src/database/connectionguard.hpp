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

    explicit ConnectionGuard(std::shared_ptr<pqxx::connection> &conn, std::atomic<bool> &is_ready, std::atomic<bool> &in_use);
    virtual ~ConnectionGuard();

   private:
    std::shared_ptr<pqxx::connection> connection;
    static constexpr std::uint64_t    CONNECTION_READY_TIMEOUT = 1;

    std::atomic<bool> &isConnectionInUse;
};
