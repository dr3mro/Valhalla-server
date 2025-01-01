#include "database/connectionguard.hpp"

#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <stdexcept>
#include <thread>

#include "utils/message/message.hpp"

ConnectionGuard::ConnectionGuard(std::shared_ptr<pqxx::connection>& conn, std::atomic<bool>& is_ready, std::atomic<bool>& in_use) /* NOLINT */
    : connection(conn), isConnectionInUse(in_use)
{
    try
    {
        auto start_time       = std::chrono::steady_clock::now();
        auto timeout_duration = std::chrono::milliseconds(5000); /* NOLINT */

        while (!is_ready.load() && !connection->is_open() && (std::chrono::steady_clock::now() - start_time) < timeout_duration)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(CONNECTION_READY_TIMEOUT));
        }

        if (!is_ready.load() && !connection->is_open())
        {
            Message::ErrorMessage("Connection not ready within timeout.");
            throw std::runtime_error("ConnectionGuard failed to acquire connection.");
        }

        isConnectionInUse.store(true, std::memory_order_relaxed);
    }
    catch (const std::exception& e)
    {
        Message::ErrorMessage(e.what());
        throw std::runtime_error("ConnectionGuard failed to acquire connection.");
    }
}

ConnectionGuard::~ConnectionGuard() { isConnectionInUse.store(false, std::memory_order_relaxed); }
