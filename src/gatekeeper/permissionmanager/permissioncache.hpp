/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file permissioncache.hpp
 * @brief A short-lived, thread-safe cache of permission data read from the
 * database. (Originally: "Idle-expiry cache for permission lookups, keyed by
 * the generated permission.")
 * @details This header defines a template cache that remembers the answer to a
 *          permission query for a while. Each use renews the entry's lifetime;
 *          unused entries expire. A generation counter prevents a slow read
 * from putting stale data back after the cache has been flushed.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Permission checks happen on almost every request. Reading them from
 * PostgreSQL each time would be slow, so PermissionManagerPrivate asks this
 * cache first. Without it, authorization would mean a database round-trip on
 * every API call.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Caching and time-to-live (TTL):
 * https://en.wikipedia.org/wiki/Cache_(computing)
 * - Mutexes and thread safety: https://en.cppreference.com/w/cpp/thread/mutex
 * - std::chrono (clocks and durations):
 * https://en.cppreference.com/w/cpp/chrono
 * - std::unordered_map (hash table):
 * https://en.cppreference.com/w/cpp/container/unordered_map
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per source file.

// std::atomic for a generation counter shared between threads.
#include <atomic>
#include <chrono>    // Clocks, time points and seconds used for expiry.
#include <cstdint>   // std::uint64_t for the generation token.
#include <iterator>  // std::next, used to advance a map iterator while erasing.
// jsoncons::json, the type of the cached permission data.
#include <jsoncons/basic_json.hpp>
#include <mutex>          // std::mutex and std::lock_guard for thread safety.
#include <optional>       // std::optional for "maybe a cached value".
#include <string>         // std::string for the cache key (the query text).
#include <unordered_map>  // The underlying hash table that stores the entries.

// PermissionCacheConfig, the source of the TTL.
#include "configurator/configurator.hpp"
// Store, used to reach the global Configurator in instance().
#include "store/store.hpp"

/**
 * @class BasicPermissionCache
 * @brief Idle-expiry cache for permission lookups, keyed by the generated
 * permission query (which embeds the entity table and id).
 * @details Each hit renews the entry's lifetime; an entry that is not used for
 * the TTL is removed. The TTL comes from Configurator::PermissionCacheConfig
 *          (PERMISSION_CACHE_TTL, default 300 seconds); 0 disables the cache.
 *
 *          Security trade-off: there is no invalidation on writes, so a revoked
 *          permission can keep being served while its entry stays in use.
 * invalidateAll() is available for callers that need an explicit flush.
 *
 *          All public operations are thread-safe via mutex_; generation_ is
 * atomic so readers can take a token without locking and have a racing put()
 * rejected. Think of it as a notepad: look up a question, and if the answer is
 * still fresh use it; otherwise ask the database and jot the new answer down.
 * @tparam ClockT Clock used for idle expiry; defaults to
 * std::chrono::steady_clock.
 */
template <typename ClockT = std::chrono::
              steady_clock>  // The clock is a template parameter mainly so
                             // tests can substitute a fake clock.
class BasicPermissionCache   // "Basic" because a
                             // convenience alias
                             // with the default
                             // clock is provided at
                             // the end.
{
 public:                // Everything below is callable by users of the cache.
  using Clock = ClockT; /**< Clock type used for expiry timestamps; a local
                           nickname for readability. */
  using TimePoint =
      typename Clock::time_point; /**< Timestamp type produced by Clock; a
                                     local nickname used for deadlines. */

  /**
   * @brief Constructs a cache with the given idle TTL and capacity.
   * @param[in] ttl Idle lifetime; a value of zero disables the cache
   * entirely.
   * @param[in] max_entries Capacity; when reached on put(), the whole map is
   * cleared.
   */
  explicit BasicPermissionCache(std::chrono::seconds ttl,
                                size_t               max_entries = 10000)
      : ttl_(ttl), max_entries_(max_entries)
  {
  }  // Stores the idle lifetime and capacity; "explicit" prevents accidental
     // implicit construction.

  /** @brief Whether caching is active (i.e. the TTL is positive). */
  [[nodiscard]] bool enabled() const
  {
    return ttl_.count() > 0;
  }  // Caching is on only when the TTL is a positive number of seconds.

  /**
   * Token to pass to put(); a put with a stale token is dropped, so a read
   * that raced with a write cannot store pre-write data.
   */
  [[nodiscard]] uint64_t generation() const
  {
    return generation_.load(std::memory_order_acquire);
  }  // Reads the current epoch without locking; acquire pairs with the
     // release in invalidateAll().

  /**
   * @brief Looks up @p key, renewing the entry's idle timer on a hit.
   * @param[in] key Generated permission query string.
   * @return The cached JSON on a live hit; `std::nullopt` when disabled,
   * absent, or expired (expired entries are erased).
   */
  std::optional<jsoncons::json> get(const std::string& key)
  {
    if (!enabled())  // If caching is disabled, never pretend to have a
                     // cached answer.
    {
      return std::nullopt;  // No value to give back.
    }
    std::lock_guard<std::mutex> lock(
        mutex_);  // Lock for the whole lookup so no other thread can change
                  // the map mid-read.
    auto it = map_.find(key);  // Look for an entry with this query string.
    if (it == map_.end())      // Not found: a plain cache miss.
    {
      return std::nullopt;  // Caller should read from the database.
    }
    if (Clock::now() >=
        it->second.expires)  // Found but too old (idle past its TTL).
    {
      map_.erase(it);  // Remove the stale entry so it cannot be used again.
      return std::nullopt;  // Treat a stale entry as a miss.
    }
    it->second.expires =
        Clock::now() + ttl_;  // used: renew the idle timer because this
                              // entry was just used.
    return it->second.value;  // Hand back the cached JSON by value.
  }

  /**
   * @brief Stores @p value under @p key if the cache is still in the same
   * generation.
   * @details The generation check prevents a read that raced with an
   * invalidation from re-inserting pre-write data. Before inserting, expired
   * entries are swept and, if the map is full, all entries are cleared.
   * @param[in] key Generated permission query string.
   * @param[in] value Permissions JSON returned by the database.
   * @param[in] generation_at_read Token from generation() taken before the
   * read.
   */
  void put(const std::string&    key,
           const jsoncons::json& value,
           uint64_t              generation_at_read)
  {
    if (!enabled())  // Nothing to store when caching is off.
    {
      return;  // Do nothing.
    }
    std::lock_guard<std::mutex> lock(
        mutex_);  // Guard the map for the whole operation.
    if (generation_at_read !=
        generation_.load(
            std::memory_order_acquire))  // A flush happened while the
                                         // database read was in flight.
    {
      return;  // Drop this stale result instead of re-inserting old data.
    }
    sweepExpired();  // First try to make room by removing already-expired
                     // entries.
    if (map_.size() >= max_entries_)  // Still full: hit the capacity limit.
    {
      map_.clear();  // Simplest policy: throw everything away to bound
                     // memory use.
    }
    map_[key] = Entry{value,
                      Clock::now() +
                          ttl_};  // Store the value with a fresh idle deadline.
  }

  /**
   * @brief Flushes all entries and bumps the generation so any pending stale
   * put() is dropped.
   * @details Callers can use this for an explicit invalidation (e.g. after a
   * permission write), since the cache otherwise has no write-triggered
   * invalidation.
   */
  void invalidateAll()
  {
    std::lock_guard<std::mutex> lock(
        mutex_);  // Serialize the flush with reads and writes.
    generation_
        .fetch_add(1,
                   std::memory_order_acq_rel);  // Bump the epoch so any
                                                // in-flight put() with the old
                                                // token is rejected.
    map_.clear();                               // Remove every cached entry.
  }

  /** @brief Number of entries currently held (including any not yet swept).
   */
  [[nodiscard]] size_t size()
  {
    std::lock_guard<std::mutex> lock(
        mutex_);         // Lock so the size is read consistently.
    return map_.size();  // Number of entries currently held (expired ones
                         // may still be present until swept).
  }

  /**
   * @brief Returns the process-wide singleton, configured from Configurator.
   * @details The singleton's TTL is fixed at first use from
   *          Configurator::PermissionCacheConfig; thread-safe initialization
   * is guaranteed by the function-local static.
   */
  static BasicPermissionCache& instance()
  {
    static BasicPermissionCache cache(
        Store::getObject<Configurator>()
            ->get<Configurator::PermissionCacheConfig>()
            .ttl);  // Created once on first use; C++ guarantees thread-safe
                    // initialization of a function-local static.
    return cache;   // Every caller shares this one process-wide cache.
  }

 private:
  /**
   * @brief Erases entries idle past the TTL, at most once per minute.
   * @details Rate-limited via next_sweep_ to avoid a full scan on every
   * put(); expired entries that are looked up are still erased immediately by
   * get().
   * @pre Caller holds mutex_.
   */
  // Drop entries that have been idle past the TTL (at most once a minute).
  void sweepExpired()
  {
    const auto now = Clock::now();  // One timestamp for the whole sweep so
                                    // the cutoff is consistent.
    if (now < next_sweep_)  // Rate limit: do not scan more than about once
                            // a minute.
    {
      return;  // Not time yet; skip the work.
    }
    next_sweep_ =
        now + std::chrono::seconds{60};  // Schedule the next allowed sweep.
    for (auto it = map_.begin();
         it != map_.end();)  // Walk every entry; note there is no ++it in
                             // the header.
    {
      it = (now >= it->second.expires)
               ? map_.erase(it)
               : std::next(it);  // Erase expired entries; erase() returns the
                                 // next iterator, otherwise advance manually.
    }
  }

  /** @brief A cached permissions value together with its idle-expiry
   * deadline. */
  struct Entry
  {
    jsoncons::json value; /**< Cached permissions JSON, the actual answer we
                             are storing. */
    TimePoint expires;    /**< Idle deadline; renewed on every hit so busy
                             entries never expire. */
  };

  std::chrono::seconds ttl_; /**< Idle lifetime; zero disables caching. Set
                                once in the constructor. */
  size_t
      max_entries_;  /**< Capacity before a full clear; bounds memory usage. */
  std::mutex mutex_; /**< Guards map_ and next_sweep_ so threads cannot
                        corrupt them. */
  std::atomic<uint64_t> generation_{
      0}; /**< Bumped on invalidateAll() to reject stale puts; atomic so
             readers need no lock. */
  std::unordered_map<std::string, Entry>
      map_; /**< Cache contents keyed by query string, the actual hash table.
             */
  TimePoint next_sweep_{}; /**< Earliest time of the next sweepExpired() scan;
                              default-constructed to the clock's epoch. */
};

/** @brief Convenience alias for the default-clock permission cache. */
using PermissionCache =
    BasicPermissionCache<>;  // Most code uses this name; the template stays
                             // open for tests with a fake clock.
