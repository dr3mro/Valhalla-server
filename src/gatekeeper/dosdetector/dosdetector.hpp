#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "configurator/configurator.hpp"
#include "store/store.hpp"

class DOSDetector
{
   public:
    enum Status : uint8_t
    {
        ALLOWED,
        WHITELISTED,
        BLACKLISTED,
        RATELIMITED,
        BANNED,
        ERROR
    };
    using Request = struct Request
    {
        std::string                                  ip;
        std::string                                  method;
        std::string                                  path;
        std::unordered_map<std::string, std::string> headers;
        std::string_view                             body;
    };

    /// Injectable clock (test seam). Returns a steady_clock time point.
    using ClockFn = std::function<std::chrono::time_point<std::chrono::steady_clock>()>;

    DOSDetector();
    /// Test seam: fully specified config + clock. Production behaviour is
    /// unchanged; the default constructor still reads the Store Configurator
    /// and uses the real steady clock.
    DOSDetector(Configurator::DOSDetectorConfig config, ClockFn clock);
    DOSDetector(const DOSDetector&)            = delete;
    DOSDetector(DOSDetector&&)                 = delete;
    DOSDetector& operator=(const DOSDetector&) = delete;
    DOSDetector& operator=(DOSDetector&&)      = delete;
    virtual ~DOSDetector();
    DOSDetector::Status is_dos_attack(const Request& request);

   private:
    std::shared_ptr<Configurator>                  configurator_ = Store::getObject<Configurator>();
    std::optional<Configurator::DOSDetectorConfig> owned_config_;
    const Configurator::DOSDetectorConfig&         config_             = configurator_->get<Configurator::DOSDetectorConfig>();
    ClockFn                                        clock_              = [] { return std::chrono::steady_clock::now(); };
    static constexpr int                           REQUEST_BUFFER_SIZE = 4096;
    inline void __attribute((always_inline))       clean_requests(const std::chrono::time_point<std::chrono::steady_clock>& window);
    inline void __attribute((always_inline))       clean_ratelimited_ips(const std::chrono::time_point<std::chrono::steady_clock>& now);
    inline void __attribute((always_inline))       clean_banned_ips(const std::chrono::time_point<std::chrono::steady_clock>& now);

    //                     // IP                       // Hash of Request //
    //                     times of requests
    std::unordered_map<std::string, std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>>> requests_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point>                                              ratelimited_ips_;  // time to unblock
    std::unordered_map<std::string, std::chrono::steady_clock::time_point>                                              banned_ips_;       // time to unban
    const std::unordered_set<std::string>                                                                               whitelist_ = config_.whitelist;
    const std::unordered_set<std::string>                                                                               blacklist_ = config_.blacklist;

    std::mutex request_mutex_;
    std::mutex ratelimit_mutex_;
    std::mutex ban_mutex_;
    std::mutex whitelist_mutex_;
    std::mutex blacklist_mutex_;

    std::future<void>       async_task_clean_;
    std::atomic<bool>       running_clean_{true};
    std::mutex              clean_mutex_;  // guards the cleanup-thread wait
    std::condition_variable clean_cv_;     // lets the destructor wake the cleanup thread promptly

    void                                                           cleanUpTask();
    inline std::optional<std::string> __attribute((always_inline)) generateRequestFingerprint(const DOSDetector::Request& request);
    inline bool __attribute((always_inline))                       isWhitelisted(const std::string& remote_ip);
    inline bool __attribute((always_inline))                       isBlacklisted(const std::string& remote_ip);
    inline bool __attribute((always_inline)) regexFind(const std::string& remote_ip, const std::unordered_set<std::string>& list, std::mutex& mtx);
    inline bool __attribute((always_inline)) isBanned(const std::string& remote_ip);
    inline bool __attribute((always_inline)) isRateLimited(const std::string& remote_ip);
    template <typename Map, typename Mutex>
    inline bool __attribute((always_inline)) checkStatus(const std::string& remote_ip, Map& ip_map, Mutex& mtx);

    Status processRequest(const Request& request);
};
