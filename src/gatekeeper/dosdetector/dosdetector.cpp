#include "dosdetector.hpp"

#include <fmt/format.h>
#include <xxhash.h>

#include <algorithm>
#include <regex>
#include <thread>

#include "utils/message/message.hpp"

DOSDetector::DOSDetector()
{
    try
    {
        async_task_clean_ = std::async(std::launch::async, &DOSDetector::cleanUpTask, this);
    }
    catch (const std::exception &e)
    {
        Message::ErrorMessage("Exception during DOSDetector initialization.");
        Message::CriticalMessage(e.what());
    }
}

DOSDetector::~DOSDetector()
{
    try
    {
        running_clean_.store(false);
        if (async_task_clean_.valid())
        {
            async_task_clean_.wait();
        }
    }
    catch (const std::exception &e)
    {
        Message::ErrorMessage("Exception during DOSDetector destruction.");
        Message::CriticalMessage(e.what());
    }
}

DOSDetector::Status DOSDetector::is_dos_attack(const Request &&request)
{
    try
    {
        if (isWhitelisted(request.ip))
        {
            return Status::WHITELISTED;
        }
        if (isBlacklisted(request.ip))
        {
            return Status::BLACKLISTED;
        }

        if (isRateLimited(request.ip))
        {
            processRequest(request);
            return Status::RATELIMITED;
        }

        if (isBanned(request.ip))
        {
            return Status::BANNED;
        }

        return processRequest(request);
    }
    catch (const std::exception &e)
    {
        Message::ErrorMessage("Failure in is_dos_attack.");
        Message::CriticalMessage(e.what());
        return Status::ERROR;
    }
}
inline void __attribute((always_inline))
DOSDetector::clean_requests(const std::chrono::time_point<std::chrono::steady_clock> &window)  // Cleanup requests and blocked IPs
{
    std::lock_guard<std::mutex> request_lock(request_mutex_);
    // Cleanup requests
    for (auto &request : requests_)
    {
        auto &requests = request.second;

        for (auto it = requests.begin(); it != requests.end();
             /* no increment here */)
        {
            auto &times = it->second;

            while (!times.empty() && times.front() < window)
            {
                times.pop_front();
            }

            if (times.empty())
            {
                it = requests.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}
inline void __attribute((always_inline)) DOSDetector::clean_ratelimited_ips(const std::chrono::time_point<std::chrono::steady_clock> &now)
{
    std::lock_guard<std::mutex> block_lock(ratelimit_mutex_);
    for (auto it = ratelimited_ips_.begin(); it != ratelimited_ips_.end();
         /* no increment here */)
    {
        if (now >= it->second)
        {
            // fmt::print("Removing ratelimited IP : {}\n", it->first);
            it = ratelimited_ips_.erase(it);  // Unblock IP
        }
        else
        {
            ++it;
        }
    }
}
inline void __attribute((always_inline)) DOSDetector::clean_banned_ips(const std::chrono::time_point<std::chrono::steady_clock> &now)
{
    std::lock_guard<std::mutex> ban_lock(ban_mutex_);
    for (auto it = banned_ips_.begin(); it != banned_ips_.end();
         /* no increment here */)
    {
        if (now >= it->second)
        {
            it = banned_ips_.erase(it);  // Unblock IP
        }
        else
        {
            ++it;
        }
    }
}
void DOSDetector::cleanUpTask()
{
    try
    {
        while (running_clean_.load())
        {
            // fmt::print("Starting clean up task\n");
            auto now    = std::chrono::steady_clock::now();
            auto next   = now + std::chrono::seconds(config_.clean_freq);
            auto window = now - config_.period;

            clean_requests(window);      // clean up requests older than time window
            clean_ratelimited_ips(now);  // clean up ratelimited ips [time stored in ratelimited_ips_ equals time to unblock]
            clean_banned_ips(now);       // clean up banned ips [time stored in banned_ips_ equals time to unblock]

            std::this_thread::sleep_until(next);
        }
    }
    catch (const std::exception &e)
    {
        Message::ErrorMessage("Exception in cleanUpTask.");
        Message::CriticalMessage(e.what());
    }
}

inline std::optional<std::string> __attribute((always_inline)) DOSDetector::generateRequestFingerprint(const DOSDetector::Request &req)
{
    try
    {
        std::string data;
        data.reserve(REQUEST_BUFFER_SIZE);

        for (const auto &header : req.headers)
        {
            data.append(header.second);
        }

        data.append(req.body);

        XXH64_hash_t hashed_key = XXH3_64bits(data.c_str(), data.size());

        return fmt::format("{}", hashed_key);
    }
    catch (const std::exception &e)
    {
        Message::ErrorMessage("Exception in generateRequestFingerprint.");
        Message::CriticalMessage(e.what());
        return std::nullopt;
    }
}

inline bool __attribute((always_inline)) DOSDetector::isWhitelisted(std::string_view remote_ip)
{
    try
    {
        return regexFind(remote_ip, whitelist_, whitelist_mutex_);
    }
    catch (const std::exception &e)
    {
        Message::ErrorMessage("Exception in isWhitelisted");
        Message::CriticalMessage(e.what());
        return false;
    }
}

inline bool __attribute((always_inline)) DOSDetector::isBlacklisted(std::string_view remote_ip)
{
    try
    {
        return regexFind(remote_ip, blacklist_, blacklist_mutex_);
    }
    catch (const std::exception &e)
    {
        Message::ErrorMessage("Exception in isBlacklisted.");
        Message::CriticalMessage(e.what());
        return false;
    }
}

inline bool __attribute((always_inline))
DOSDetector::regexFind(std::string_view remote_ip, const std::unordered_set<std::string> &list, std::mutex &mtx)
{
    try
    {
        std::lock_guard<std::mutex> lock(mtx);

        return std::ranges::any_of(list,
                                   [&remote_ip](const std::string_view &pattern)
                                   {
                                       try
                                       {
                                           std::regex regex_pattern((std::string(pattern)));
                                           return std::regex_search((std::string(remote_ip)), regex_pattern);
                                       }
                                       catch (const std::regex_error &e)
                                       {
                                           Message::ErrorMessage("Exception due to invalid regex pattern.");
                                           Message::CriticalMessage(e.what());

                                           return false;
                                       }
                                   });
    }
    catch (const std::exception &e)
    {
        Message::ErrorMessage("Exception in regexFind.");
        Message::CriticalMessage(e.what());

        return false;
    }
}

inline bool __attribute((always_inline)) DOSDetector::isBanned(std::string_view remote_ip)
{
    try
    {
        return checkStatus(remote_ip, banned_ips_, ban_mutex_);
    }
    catch (const std::exception &e)
    {
        Message::ErrorMessage("Exception in isBanned.");
        Message::CriticalMessage(e.what());

        return false;
    }
}

inline bool __attribute((always_inline)) DOSDetector::isRateLimited(std::string_view remote_ip)
{
    try
    {
        return checkStatus(remote_ip, ratelimited_ips_, ratelimit_mutex_);
    }
    catch (const std::exception &e)
    {
        Message::ErrorMessage(fmt::format("Exception in isRateLimited."));
        Message::CriticalMessage(e.what());

        return false;
    }
}

template <typename Map, typename Mutex>
inline bool __attribute((always_inline)) DOSDetector::checkStatus(std::string_view remote_ip, Map &ip_map, Mutex &mtx)
{
    try
    {
        auto                   now = std::chrono::steady_clock::now();
        std::lock_guard<Mutex> lock(mtx);

        if (ip_map.find(remote_ip.data()) != ip_map.end())
        {
            auto forget_time = ip_map.at(remote_ip.data());
            if (now < forget_time)
            {
                return true;
            }

            ip_map.erase(remote_ip.data());
        }
        return false;
    }
    catch (const std::exception &e)
    {
        Message::ErrorMessage("Exception in checkStatus.");
        Message::CriticalMessage(e.what());

        return false;
    }
}

DOSDetector::Status DOSDetector::processRequest(const Request &request)
{
    try
    {
        auto                       now                 = std::chrono::steady_clock::now();
        std::optional<std::string> remote_ip           = request.ip;
        std::optional<std::string> request_fingerprint = generateRequestFingerprint(request);

        if (!remote_ip)
        {
            fmt::print("Failed to get ip from request.\n");
            return Status::ERROR;
        }
        // fmt::print("Request from {}\n", remote_ip.value());
        if (!request_fingerprint)
        {
            fmt::print("Failed to generate request fingerprint.");
            return Status::ERROR;
        }
        // fmt::print("Request fingerprint: {}\n", request_fingerprint.value());

        {
            // fmt::print("Checking request from {}\n", remote_ip.value());
            std::lock_guard<std::mutex> request_lock(request_mutex_);

            // fmt::print("get the requests for the ip: {}\n", remote_ip.value());
            auto &ip_requests = requests_[remote_ip.value()];

            // fmt::print("get the fingerprint for the ip: {} :: {}\n", remote_ip.value(), request_fingerprint.value());
            auto &fp_requests = ip_requests[request_fingerprint.value()];

            const auto window = now - config_.period;

            // Remove old requests that are outside the time window
            while (!fp_requests.empty() && fp_requests.front() < window)
            {
                fp_requests.pop_front();
                // fmt::print("Removing old request from {}\n", remote_ip.value());
            }

            fp_requests.push_back(now);

            // fmt::print("size: fp :{} r:{}\n", fp_requests.size(), ip_requests.size());

            if (ip_requests.size() > config_.max_fingerprints)
            {
                std::lock_guard<std::mutex> block_lock(ratelimit_mutex_);
                ratelimited_ips_[remote_ip.value()] = now + config_.ratelimit_duration;
                return Status::RATELIMITED;
            }

            if (fp_requests.size() > config_.max_requests)
            {
                std::lock_guard<std::mutex> ban_lock(ban_mutex_);
                banned_ips_[remote_ip.value()] = now + config_.ban_duration;
                return Status::BANNED;
            }
            // fmt::print("request from {}: {}\n", remote_ip.value(), request_fingerprint.value());
        }
    }
    catch (const std::exception &e)
    {
        Message::ErrorMessage("Failure in processRequest.");
        Message::CriticalMessage(e.what());
        return Status::ERROR;
    }
    return Status::ALLOWED;
}
