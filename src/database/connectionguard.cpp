#include "database/connectionguard.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

ConnectionGuard::ConnectionGuard(std::shared_ptr<pqxx::connection>& conn, std::atomic<bool>& is_ready, std::atomic<bool>& in_use) /* NOLINT */
    : connection(conn), isConnectionInUse(in_use)
{
    while (!is_ready)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(CONNECTION_READY_TIMEOUT));
    }
    isConnectionInUse.store(true);
}

ConnectionGuard::~ConnectionGuard() { isConnectionInUse.store(false); }
