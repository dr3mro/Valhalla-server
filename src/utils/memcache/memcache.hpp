#include <chrono>
#include <condition_variable>
#include <list>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>

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
    MemCache()                           = delete;
    MemCache(const MemCache&)            = delete;
    MemCache& operator=(const MemCache&) = delete;

    MemCache(MemCache&& other) noexcept
        : maxSize_(other.maxSize_),
          defaultTTL_(other.defaultTTL_),
          stopCleaner_(other.stopCleaner_),
          cacheMap_(std::move(other.cacheMap_)),
          cacheList_(std::move(other.cacheList_)),
          cleanerThread_(std::move(other.cleanerThread_))
    {
        std::lock_guard<std::mutex> lock(other.mutex_);
        other.stopCleaner_ = true;
    }

    MemCache& operator=(MemCache&& other) noexcept
    {
        if (this != &other)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                std::lock_guard<std::mutex> otherLock(other.mutex_);
                maxSize_           = other.maxSize_;
                defaultTTL_        = other.defaultTTL_;
                stopCleaner_       = other.stopCleaner_;
                cacheMap_          = std::move(other.cacheMap_);
                cacheList_         = std::move(other.cacheList_);
                cleanerThread_     = std::move(other.cleanerThread_);
                other.stopCleaner_ = true;
            }
            cleanerCv_.notify_one();
            cleanerThread_.join();
        }
        return *this;
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
            auto item = cacheMap_[key];
            item->setValue(value);
            item->setExpiration(expiration);
            moveToFront(item);
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
        auto                        item = cacheMap_.find(key);
        if (item == cacheMap_.end() || item->second->getExpiration() <= Clock::now())
        {
            return std::nullopt;
        }
        moveToFront(item->second);
        return item->second->getValue();
    }

    bool remove(const std::string& key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto                        item = cacheMap_.find(key);
        if (item == cacheMap_.end())
        {
            return false;
        }

        cacheList_.erase(item->second);
        cacheMap_.erase(key);
        return true;
    }

   private:
    class CacheEntry
    {
       private:
        std::string key;
        T           value;
        TimePoint   expiration;

       public:
        CacheEntry(std::string _key, const T& _value, TimePoint _exp) : key(std::move(_key)), value(_value), expiration(_exp) {}
        [[nodiscard]] std::string getKey() const { return key; }
        T                         getValue() const { return value; }
        [[nodiscard]] TimePoint   getExpiration() const { return expiration; }
        void                      setValue(const T& _value) { this->value = _value; }
        void                      setExpiration(const TimePoint& _exp) { this->expiration = _exp; }
    };

    size_t                                                                    maxSize_;
    std::chrono::seconds                                                      defaultTTL_;
    std::unordered_map<std::string, typename std::list<CacheEntry>::iterator> cacheMap_;
    std::list<CacheEntry>                                                     cacheList_;
    std::mutex                                                                mutex_;
    std::condition_variable                                                   cleanerCv_;
    bool                                                                      stopCleaner_;
    std::thread                                                               cleanerThread_;

    void moveToFront(typename std::list<CacheEntry>::iterator& item) { cacheList_.splice(cacheList_.begin(), cacheList_, item); }

    void evictOldest()
    {
        auto last = cacheList_.end();
        --last;
        cacheMap_.erase(last->getKey());
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
                {
                    break;
                }

                auto now = Clock::now();

                while (!cacheList_.empty() && cacheList_.back().getExpiration() <= now)
                {
                    evictOldest();
                }
            }
        }
    }
};
