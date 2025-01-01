#pragma once

#include <atomic>
#include <pqxx/pqxx>

class InUseGuard
{
   public:
    InUseGuard()                              = delete;
    InUseGuard(const InUseGuard &)            = delete;
    InUseGuard(InUseGuard &&)                 = delete;
    InUseGuard &operator=(const InUseGuard &) = delete;
    InUseGuard &operator=(InUseGuard &&)      = delete;

    explicit InUseGuard(std::atomic<bool> &in_use);
    virtual ~InUseGuard();

   private:
    std::atomic<bool> &isConnectionInUse;
};
