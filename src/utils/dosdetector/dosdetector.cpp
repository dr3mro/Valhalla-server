#include "dosdetector.hpp"

#include <fmt/format.h>
#include <xxhash.h>

#include <functional>
#include <iostream>
#include <regex>
#include <thread>

DOSDetector::DOSDetector()
{
    try
    {
        async_task_clean_ = std::async(std::launch::async, &DOSDetector::cleanUpTask, this);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception during DOSDetector initialization: " << e.what() << std::endl;
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
        std::cerr << "Exception during DOSDetector destruction: " << e.what() << std::endl;
    }
}

DOSDetector::Status DOSDetector::is_dos_attack(const crow::request &req)
{
    const std::string_view remote_ip = req.remote_ip_address;
    try
    {
        if (isWhitelisted(remote_ip))
        {
            return Status::WHITELISTED;
        }
        if (isBlacklisted(remote_ip))
        {
            return Status::BLACKLISTED;
        }

        if (isRateLimited(remote_ip))
        {
            processRequest<const crow::request &>(req);
            return Status::RATELIMITED;
        }

        if (isBanned(remote_ip))
        {
            return Status::BANNED;
        }

        return processRequest<const crow::request &>(req);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Failure in is_dos_attack: " << e.what() << std::endl;
        return Status::ERROR;
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

            // Cleanup requests and blocked IPs
            {
                std::lock_guard<std::mutex> request_lock(request_mutex_);
                // Cleanup requests
                for (auto &ot : requests_)
                {
                    auto &requests = ot.second;

                    for (auto it = requests.begin(); it != requests.end();
                         /* no increment here */)
                    {
                        auto &times = it->second;

                        while (!times.empty() && times.front() < window)
                        {
                            times.pop_front();
                            // fmt::print("Removing old request from {}\n", ot.first);
                        }

                        if (times.empty())
                        {
                            it = requests.erase(it);
                            // fmt::print("Removing empty request from {}\n", ot.first);
                        }
                        else
                        {
                            ++it;
                        }
                    }
                }
            }

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

            {
                std::lock_guard<std::mutex> ban_lock(ban_mutex_);
                for (auto it = banned_ips_.begin(); it != banned_ips_.end();
                     /* no increment here */)
                {
                    if (now >= it->second)
                    {
                        // fmt::print("Removing banned IP :{}\n", it->first);
                        it = banned_ips_.erase(it);  // Unblock IP
                    }
                    else
                    {
                        ++it;
                    }
                }
            }

            std::this_thread::sleep_until(next);
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception in cleanUpTask: " << e.what() << std::endl;
    }
}

inline std::optional<std::string> __attribute((always_inline)) DOSDetector::generateRequestFingerprint(const crow::request &req)
{
    try
    {
        std::string data;
        data.reserve(4096);

        for (const auto &header : req.headers)
        {
            data.append(header.second);
        }

        data.append(req.body);

        XXH64_hash_t hashed_key = XXH3_64bits(data.c_str(), data.size());

        // fmt::print("Request fingerprint: {}\n", hashed_key);

        return fmt::format("{}", hashed_key);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception in generateRequestFingerprint: " << e.what() << std::endl;
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
        std::cerr << "Exception in isWhitelisted: " << e.what() << std::endl;
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
        std::cerr << "Exception in isBlacklisted: " << e.what() << std::endl;
        return false;
    }
}

inline bool __attribute((always_inline)) DOSDetector::regexFind(std::string_view remote_ip, const std::unordered_set<std::string> &list,
                                                                std::mutex &mtx)
{
    try
    {
        // fmt::print("{}\n", remote_ip);

        std::lock_guard<std::mutex> lock(mtx);

        return std::any_of(list.begin(), list.end(),
                           [&remote_ip](const std::string_view &pattern)
                           {
                               try
                               {
                                   std::regex regex_pattern(pattern.data());
                                   return std::regex_search(remote_ip.data(), regex_pattern);
                               }
                               catch (const std::regex_error &e)
                               {
                                   std::cerr << "Invalid regex pattern: " << e.what() << std::endl;
                                   return false;
                               }
                           });
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception in regexFind: " << e.what() << std::endl;
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
        std::cerr << "Exception in isBanned: " << e.what() << std::endl;
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
        std::cerr << "Exception in isRateLimited: " << e.what() << std::endl;
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
            else
            {
                ip_map.erase(remote_ip.data());
            }
        }
        return false;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception in checkStatus: " << e.what() << std::endl;
        return false;
    }
}

template <typename Req>
DOSDetector::Status DOSDetector::processRequest(Req &&req)
{
    try
    {
        auto                       now                 = std::chrono::steady_clock::now();
        std::optional<std::string> remote_ip           = req.remote_ip_address;
        std::optional<std::string> request_fingerprint = generateRequestFingerprint(req);

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
        std::cerr << "Failure in processRequest: " << e.what() << std::endl;
        return Status::ERROR;
    }
    return Status::ALLOWED;
}
