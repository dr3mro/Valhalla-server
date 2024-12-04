#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <future>
#include <mutex>
#include <string>
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

    DOSDetector();
    virtual ~DOSDetector();
    DOSDetector::Status is_dos_attack(const Request &&request);

   private:
    std::shared_ptr<Configurator>          configurator_       = Store::getObject<Configurator>();
    const Configurator::DOSDetectorConfig &config_             = configurator_->get<Configurator::DOSDetectorConfig>();
    static constexpr int                   REQUEST_BUFFER_SIZE = 4096;
    inline void __attribute((always_inline)) clean_requests(const std::chrono::time_point<std::chrono::steady_clock> &window);
    inline void __attribute((always_inline)) clean_ratelimited_ips(const std::chrono::time_point<std::chrono::steady_clock> &now);
    inline void __attribute((always_inline)) clean_banned_ips(const std::chrono::time_point<std::chrono::steady_clock> &now);

    //                     // IP                       // Hash of Request // times
    //                     of requests
    std::unordered_map<std::string, std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>>> requests_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> ratelimited_ips_;  // time to unblock
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> banned_ips_;       // time to unban
    const std::unordered_set<std::string>                                  whitelist_ = config_.whitelist;
    const std::unordered_set<std::string>                                  blacklist_ = config_.blacklist;

    std::mutex request_mutex_;
    std::mutex ratelimit_mutex_;
    std::mutex ban_mutex_;
    std::mutex whitelist_mutex_;
    std::mutex blacklist_mutex_;

    std::future<void> async_task_clean_;
    std::atomic<bool> running_clean_{true};

    void cleanUpTask();
    inline std::optional<std::string> __attribute((always_inline)) generateRequestFingerprint(const DOSDetector::Request &request);
    inline bool __attribute((always_inline)) isWhitelisted(std::string_view remote_ip);
    inline bool __attribute((always_inline)) isBlacklisted(std::string_view remote_ip);
    inline bool __attribute((always_inline)) regexFind(std::string_view remote_ip, const std::unordered_set<std::string> &list, std::mutex &mtx);
    inline bool __attribute((always_inline)) isBanned(std::string_view remote_ip);
    inline bool __attribute((always_inline)) isRateLimited(std::string_view remote_ip);
    template <typename Map, typename Mutex>
    inline bool __attribute((always_inline)) checkStatus(std::string_view remote_ip, Map &ip_map, Mutex &mtx);

    Status processRequest(const Request &request);
};
