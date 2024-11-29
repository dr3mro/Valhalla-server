#include <chrono>
#include <condition_variable>
#include <iostream>
#include <list>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>

template <typename T>
class MemCache
{
   public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = std::chrono::time_point<Clock>;

    MemCache(size_t maxSize, std::chrono::seconds defaultTTL) : maxSize_(maxSize), defaultTTL_(defaultTTL), stopCleaner_(false)
    {
        cleanerThread_ = std::thread(&MemCache::cleanExpiredEntries, this);
    }

    ~MemCache()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopCleaner_ = true;
        }
        cleanerCv_.notify_one();
        cleanerThread_.join();
    }

    void insert(const std::string& key, const T& value, std::chrono::seconds ttl = std::chrono::seconds::zero())
    {
        TimePoint                   expiration = Clock::now() + (ttl == std::chrono::seconds::zero() ? defaultTTL_ : ttl);
        std::lock_guard<std::mutex> lock(mutex_);

        if (cacheMap_.find(key) != cacheMap_.end())
        {
            // Update value and expiration
            auto it        = cacheMap_[key];
            it->value      = value;
            it->expiration = expiration;
            moveToFront(it);
        }
        else
        {
            if (cacheMap_.size() >= maxSize_)
            {
                evictOldest();
            }

            cacheList_.emplace_front(key, value, expiration);
            auto begin_it  = cacheList_.begin();  // This is the correct iterator type
            cacheMap_[key] = begin_it;            // Assign the iterator, not the entry itself
        }
    }

    std::optional<T> get(const std::string& key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto                        it = cacheMap_.find(key);
        if (it == cacheMap_.end() || it->second->expiration <= Clock::now())
        {
            return std::nullopt;
        }
        moveToFront(it->second);
        return it->second->value;
    }

    bool remove(const std::string& key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto                        it = cacheMap_.find(key);
        if (it == cacheMap_.end())
        {
            return false;
        }

        cacheList_.erase(it->second);
        cacheMap_.erase(key);
        return true;
    }

   private:
    struct CacheEntry
    {
        std::string key;
        T           value;
        TimePoint   expiration;

        CacheEntry(const std::string& k, const T& v, TimePoint exp) : key(k), value(v), expiration(exp) {}
    };

    size_t                                                                    maxSize_;
    std::chrono::seconds                                                      defaultTTL_;
    std::unordered_map<std::string, typename std::list<CacheEntry>::iterator> cacheMap_;
    std::list<CacheEntry>                                                     cacheList_;
    std::mutex                                                                mutex_;
    std::condition_variable                                                   cleanerCv_;
    bool                                                                      stopCleaner_;
    std::thread                                                               cleanerThread_;

    void moveToFront(typename std::list<CacheEntry>::iterator& it) { cacheList_.splice(cacheList_.begin(), cacheList_, it); }

    void evictOldest()
    {
        auto last = cacheList_.end();
        --last;
        cacheMap_.erase(last->key);
        cacheList_.pop_back();
    }

    void cleanExpiredEntries()
    {
        while (true)
        {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cleanerCv_.wait_for(lock, std::chrono::seconds(1), [this] { return stopCleaner_; });
                if (stopCleaner_)
                    break;
                auto now = Clock::now();
                while (!cacheList_.empty() && cacheList_.back().expiration <= now)
                {
                    evictOldest();
                }
            }
        }
    }
};
