#include <atomic>
#include <condition_variable>
#include <mutex>

class InUseGuard
{
   public:
    InUseGuard(const InUseGuard&)            = default;
    InUseGuard(InUseGuard&&)                 = delete;
    InUseGuard& operator=(const InUseGuard&) = delete;
    InUseGuard& operator=(InUseGuard&&)      = delete;
    InUseGuard(std::atomic<bool>& in_use, std::mutex& mtx, std::condition_variable& _cv);

    ~InUseGuard();

   private:
    std::atomic<bool>&       isConnectionInUse_; /*NOLINT*/
    std::mutex&              mutex_;             /*NOLINT*/
    std::condition_variable& conditionVar_;      /*NOLINT*/
};
