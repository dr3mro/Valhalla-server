#pragma once
#include <crow.h>

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

    DOSDetector();
    virtual ~DOSDetector();
    DOSDetector::Status is_dos_attack(const crow::request &req);

   private:
    std::shared_ptr<Configurator>          configurator_ = Store::getObject<Configurator>();
    const Configurator::DODDetectorConfig &config_       = configurator_->get<Configurator::DODDetectorConfig>();

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

    void                                                           cleanUpTask();
    inline std::optional<std::string> __attribute((always_inline)) generateRequestFingerprint(const crow::request &req);
    inline bool __attribute((always_inline))                       isWhitelisted(std::string_view remote_ip);
    inline bool __attribute((always_inline))                       isBlacklisted(std::string_view remote_ip);
    inline bool __attribute((always_inline)) regexFind(std::string_view remote_ip, const std::unordered_set<std::string> &list, std::mutex &mtx);
    inline bool __attribute((always_inline)) isBanned(std::string_view remote_ip);
    inline bool __attribute((always_inline)) isRateLimited(std::string_view remote_ip);
    template <typename Map, typename Mutex>
    inline bool __attribute((always_inline)) checkStatus(std::string_view remote_ip, Map &ip_map, Mutex &mtx);

    template <typename Req>
    DOSDetector::Status processRequest(Req &&req);
};
