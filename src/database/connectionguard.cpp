#include "database/connectionguard.hpp"

#include <chrono>
#include <memory>
#include <thread>

ConnectionGuard::ConnectionGuard(std::shared_ptr<pqxx::connection> &conn) : connection(conn)
{
    is_ready.store(true);
    in_use.store(false);
}

ConnectionGuard::~ConnectionGuard() { is_ready.store(false); }

void ConnectionGuard::setInUse(bool state) { in_use.store(state); }

void ConnectionGuard::setIsRead(bool state) { is_ready.store(state); }

bool ConnectionGuard::getInUse() { return in_use.load(); }

bool ConnectionGuard::getIsReady() { return is_ready.load(); }

void ConnectionGuard::waitForConnection()
{
    while (!is_ready.load() || in_use.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(CONNECTION_READY_TIMEOUT));
    }
}
