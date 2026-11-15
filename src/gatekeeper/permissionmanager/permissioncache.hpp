#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <jsoncons/basic_json.hpp>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "configurator/configurator.hpp"
#include "store/store.hpp"

/// Idle-expiry cache for permission lookups, keyed by the generated permission
/// query (which embeds the entity table and id).
///
/// Each hit renews the entry's lifetime; an entry that is not used for the TTL
/// is removed. The TTL comes from Configurator::PermissionCacheConfig
/// (PERMISSION_CACHE_TTL, default 300 seconds); 0 disables the cache.
///
/// Security trade-off: there is no invalidation on writes, so a revoked
/// permission can keep being served while its entry stays in use. invalidateAll()
/// is available for callers that need an explicit flush.
template <typename ClockT = std::chrono::steady_clock>
class BasicPermissionCache
{
   public:
    using Clock     = ClockT;
    using TimePoint = typename Clock::time_point;

    explicit BasicPermissionCache(std::chrono::seconds ttl, size_t max_entries = 10000) : ttl_(ttl), max_entries_(max_entries) {}

    [[nodiscard]] bool enabled() const { return ttl_.count() > 0; }

    /// Token to pass to put(); a put with a stale token is dropped, so a read
    /// that raced with a write cannot store pre-write data.
    [[nodiscard]] uint64_t generation() const { return generation_.load(std::memory_order_acquire); }

    std::optional<jsoncons::json> get(const std::string& key)
    {
        if (!enabled())
        {
            return std::nullopt;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        auto                        it = map_.find(key);
        if (it == map_.end())
        {
            return std::nullopt;
        }
        if (Clock::now() >= it->second.expires)
        {
            map_.erase(it);
            return std::nullopt;
        }
        it->second.expires = Clock::now() + ttl_;  // used: renew the idle timer
        return it->second.value;
    }

    void put(const std::string& key, const jsoncons::json& value, uint64_t generation_at_read)
    {
        if (!enabled())
        {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation_at_read != generation_.load(std::memory_order_acquire))
        {
            return;
        }
        sweepExpired();
        if (map_.size() >= max_entries_)
        {
            map_.clear();
        }
        map_[key] = Entry{value, Clock::now() + ttl_};
    }

    void invalidateAll()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        generation_.fetch_add(1, std::memory_order_acq_rel);
        map_.clear();
    }

    [[nodiscard]] size_t size()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return map_.size();
    }

    static BasicPermissionCache& instance()
    {
        static BasicPermissionCache cache(Store::getObject<Configurator>()->get<Configurator::PermissionCacheConfig>().ttl);
        return cache;
    }

   private:
    // Drop entries that have been idle past the TTL (at most once a minute).
    void sweepExpired()
    {
        const auto now = Clock::now();
        if (now < next_sweep_)
        {
            return;
        }
        next_sweep_ = now + std::chrono::seconds{60};
        for (auto it = map_.begin(); it != map_.end();)
        {
            it = (now >= it->second.expires) ? map_.erase(it) : std::next(it);
        }
    }

    struct Entry
    {
        jsoncons::json value;
        TimePoint      expires;
    };

    std::chrono::seconds                   ttl_;
    size_t                                 max_entries_;
    std::mutex                             mutex_;
    std::atomic<uint64_t>                  generation_{0};
    std::unordered_map<std::string, Entry> map_;
    TimePoint                              next_sweep_{};
};

using PermissionCache = BasicPermissionCache<>;
