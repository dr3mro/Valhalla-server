#include "database/inuseguard.hpp"

#include <atomic>
#include <chrono>
#include <thread>

InUseGuard::InUseGuard(std::atomic<bool>& in_use) : isConnectionInUse(in_use)
{
    while (isConnectionInUse.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    isConnectionInUse.store(true);
}

InUseGuard::~InUseGuard() { isConnectionInUse.store(false); }
