/**
 * @file memcache.hpp
 * @brief Thread-safe in-memory LRU cache with TTL expiry and a cleaner thread.
 * @details Defines a template MemCache<T, ClockT> that stores values by key in
 *          memory. A hash map maps keys to positions in a list, giving
 * constant- time lookup and cheap reordering: the list is kept in "most
 * recently used first" order so the least recently used item can be evicted
 * when capacity is reached. Each entry has a time-to-live (TTL); a background
 *          thread periodically removes expired entries, and get() treats
 * expired entries as misses. A single mutex protects all state, so the cache is
 *          safe to share between threads. Move and destruction stop the cleaner
 *          thread before any state is touched.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Some data is expensive to fetch (for example permissions or database results)
 * but does not change often. This cache lets the server keep a bounded,
 * temporary copy in memory so repeated requests are fast. The TTL ensures stale
 * data does not live forever, and the LRU limit stops it from growing without
 * bound. It is a self-contained header-only utility, used wherever a caller
 * wants a safe, time-limited store; without it every caller would build its own
 * cache and duplicate the expiry and thread-safety logic.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - std::unordered_map:
 * https://en.cppreference.com/w/cpp/container/unordered_map
 * - std::list: https://en.cppreference.com/w/cpp/container/list
 * - std::condition_variable:
 * https://en.cppreference.com/w/cpp/thread/condition_variable
 * - std::thread and join: https://en.cppreference.com/w/cpp/thread/thread/join
 * - std::chrono time points:
 * https://en.cppreference.com/w/cpp/chrono/time_point
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Provides clocks, time points and seconds used for TTL calculations.
#include <chrono>
// The cleaner waits on a condition variable so it can be woken early on
// shutdown/move.
#include <condition_variable>
// std::list holds entries in recency order; its iterators stay valid across
// splices.
#include <list>
#include <mutex>  // std::mutex and std::lock_guard protect all shared state.
// std::optional is the return type of get(): a value or "no entry".
#include <optional>
#include <thread>  // std::thread runs the background cleaner loop.
// Maps keys to list iterators for constant-time lookup.
#include <unordered_map>
// std::move is used throughout to transfer state efficiently.
#include <utility>

/**
 * @class MemCache
 * @brief Bounded LRU cache whose entries expire after a configurable TTL.
 * @details Keys map to iterators into a std::list, giving O(1) lookup,
 *          recency promotion and eviction. A background thread periodically
 *          erases expired entries; insert() independently evicts the least
 *          recently used entry when capacity is reached. All public operations
 *          and the cleaner share a single mutex, so they are safe to call
 *          concurrently.
 *
 *          The cleaner thread holds a raw pointer to `this`. Move operations
 * stop and join both the source's and destination's cleaners before touching
 *          state; the destructor does the same before members are destroyed.
 * @tparam T Value type; stored and returned by value, so it must be copyable.
 * @tparam ClockT Clock exposing a static now(); injectable so tests can use a
 *          manually advanced clock instead of waiting in real time.
 * @note get() treats an expired entry as a miss but does not erase it; physical
 *       removal is left to the cleaner. remove() does not check expiry.
 */
// T is the stored value type; ClockT can be
// swapped in tests to control time.
// A thread-safe, expiring,
// size-bounded cache.
template <typename T, typename ClockT = std::chrono::steady_clock>
class MemCache {
 public:
  /**
   * @brief Clock type used for expiration timestamps.
   */
  using Clock = ClockT;  // Alias so the rest of the class refers to the
                         // chosen clock by one short name.
  /**
   * @brief Time point type used for expiration timestamps.
   */
  using TimePoint =
      std::chrono::time_point<Clock>;  // A time point measured on Clock, used
                                       // as each entry's expiry stamp.

  /**
   * @brief Constructs a cache and immediately starts its cleaner thread.
   * @param[in] maxSize Maximum number of entries before LRU eviction.
   * @param[in] defaultTTL TTL applied by insert() when no explicit TTL is
   * given.
   */
  MemCache(size_t maxSize, std::chrono::seconds defaultTTL)
      // Initialises the limits and the "keep running"
      // flag; the list/map start empty.
      : maxSize_(maxSize), defaultTTL_(defaultTTL), stopCleaner_(false) {
    cleanerThread_ =
        std::thread(&MemCache::cleanExpiredEntries,
                    this);  // Start the background cleaner, passing this object
                            // so it can reach the state.
  }
  MemCache() = delete;  // No default cache: capacity and TTL must be provided
                        // explicitly.
  MemCache(const MemCache&) =
      delete;  // Copying would duplicate a thread holding a raw pointer to
               // `this`, so copying is forbidden.
  MemCache& operator=(const MemCache&) =
      delete;  // Copy assignment is forbidden for the same reason.

  /**
   * @brief Transfers cache state from `other` and starts a fresh cleaner.
   * @param[in,out] other Source cache; its cleaner is stopped and joined
   * before its state is moved.
   * @post `other` has no running cleaner and is left in a valid but
   * unspecified state; `*this` owns the transferred entries and a new cleaner
   * thread.
   */
  MemCache(MemCache&& other) noexcept
      // Move constructor copies the simple settings; a
      // new cleaner will be started for this object.
      : maxSize_(other.maxSize_),
        defaultTTL_(other.defaultTTL_),
        stopCleaner_(false) {
    // Stop the source's cleaner before moving state so the thread never
    // runs against the moved-from object.
    {
      std::lock_guard<std::mutex> lock(
          other.mutex_);  // Lock the source while flipping its stop flag.
      other.stopCleaner_ = true;  // Ask the source's cleaner to exit.
    }
    other.cleanerCv_
        .notify_one();  // Wake the source's cleaner so it notices the stop
                        // flag without waiting a full second.
    // Only join a thread that is actually running.
    if (other.cleanerThread_.joinable()) {
      other.cleanerThread_.join();  // Wait for the source's cleaner to
                                    // finish before its state is moved.
    }

    {
      std::lock_guard<std::mutex> lock(
          other.mutex_);  // Lock the source again while transferring its
                          // containers.
      cacheMap_ = std::move(other.cacheMap_);  // Move the key-to-iterator
                                               // index into this object.
      cacheList_ =
          std::move(other.cacheList_);  // Move the entry list into this object.
    }

    cleanerThread_ =
        std::thread(&MemCache::cleanExpiredEntries,
                    this);  // Start a fresh cleaner bound to the new object.
  }

  /**
   * @brief Replaces this cache with the state of `other` via move assignment.
   * @param[in,out] other Source cache; its cleaner is also stopped and
   * joined.
   * @return `*this`.
   * @note Self-assignment is a no-op. Both cleaners are stopped before any
   *       state is moved, and a fresh cleaner is started for `*this`.
   */
  // Move assignment transfers another cache's
  // state into this one.
  MemCache& operator=(MemCache&& other) noexcept {
    // Guard against assigning an object to itself,
    // which would destroy its own state.
    if (this != &other) {
      // Stop this object's cleaner first.
      {
        std::lock_guard<std::mutex> lock(
            mutex_);          // Lock this object while setting its stop flag.
        stopCleaner_ = true;  // Ask this object's cleaner to exit.
      }
      cleanerCv_.notify_one();  // Wake it immediately.
      // Check that there is a thread to join.
      if (cleanerThread_.joinable()) {
        cleanerThread_.join();  // Wait for this object's cleaner to stop.
      }

      // Stop the source's cleaner.
      {
        std::lock_guard<std::mutex> lock(
            other.mutex_);          // Lock the source while setting its stop
                                    // flag.
        other.stopCleaner_ = true;  // Ask the source's cleaner to exit.
      }
      other.cleanerCv_.notify_one();  // Wake the source's cleaner immediately.
      // Check that the source has a thread to join.
      if (other.cleanerThread_.joinable()) {
        other.cleanerThread_.join();  // Wait for the source's cleaner to stop.
      }

      {
        std::lock_guard<std::mutex> lockThis(mutex_);  // Lock this object...
        std::lock_guard<std::mutex> lockOther(
            other.mutex_);             // ...and the source, so both states are
                                       // consistent during the move.
        maxSize_    = other.maxSize_;  // Copy the size limit.
        defaultTTL_ = other.defaultTTL_;  // Copy the default TTL.
        cacheMap_ =
            std::move(other.cacheMap_);  // Move the lookup index across.
        cacheList_ =
            std::move(other.cacheList_);  // Move the entry list across.
        stopCleaner_ = false;  // Reset the flag because a new cleaner
                               // will run here.
      }

      cleanerThread_ =
          std::thread(&MemCache::cleanExpiredEntries,
                      this);  // Start a fresh cleaner for this object.
    }
    return *this;  // Return this object so assignments can be chained.
  }

  /**
   * @brief Stops the cleaner and waits for it before members are destroyed.
   */
  // Destructor shuts the cleaner down before the cache
  // members disappear.
  virtual ~MemCache() {
    {
      std::lock_guard<std::mutex> lock(
          mutex_);          // Lock while flipping the stop flag.
      stopCleaner_ = true;  // Request cleaner termination.
    }
    cleanerCv_.notify_one();  // Wake the cleaner so it exits promptly.
    // Check that the cleaner thread is still running.
    if (cleanerThread_.joinable()) {
      cleanerThread_.join();  // Wait for it to finish; otherwise it would
                              // touch destroyed members.
    }
  }

  /**
   * @brief Inserts a value or refreshes an existing key.
   * @param[in] key Key to insert or update.
   * @param[in] value Value to store; copied into the cache.
   * @param[in] ttl Lifetime in seconds; zero selects defaultTTL_.
   * @details Existing keys receive a new value, a fresh expiration and move
   * to the front of the LRU list. New keys are added at the front; when
   *          capacity is already reached the least recently used entry is
   *          evicted first. Safe to call concurrently with other operations.
   */
  // Adds or refreshes an entry; a zero
  // ttl means "use the default".
  void insert(const std::string&   key,
              const T&             value,
              std::chrono::seconds ttl = std::chrono::seconds::zero()) {
    TimePoint expiration =
        Clock::now() + (ttl == std::chrono::seconds::zero()
                            ? defaultTTL_
                            : ttl);  // Compute when the entry dies: now
                                     // plus the chosen TTL.
    std::lock_guard<std::mutex> lock(
        mutex_);  // Lock for the whole operation so state stays consistent.

    // Check whether this key is already present.
    if (cacheMap_.find(key) != cacheMap_.end()) {
      // Update value and expiration
      auto item = cacheMap_[key];  // Get the list iterator stored for
                                   // this key (cacheMap_ uses [] which
                                   // returns the existing entry).
      item->setValue(value);       // Replace the stored value with the new one.
      item->setExpiration(expiration);  // Give the entry a fresh expiry time.
      moveToFront(item);                // Mark it as most recently used.
    } else {
      // If the cache is full, make room first.
      if (cacheMap_.size() >= maxSize_) {
        evictOldest();  // Remove the least recently used entry.
      }

      cacheList_.emplace_front(key,
                               value,
                               expiration);  // Create the new entry at the
                                             // front (most recent) of the list.
      auto begin_it  = cacheList_.begin();  // This is the correct iterator type
      cacheMap_[key] = begin_it;  // Assign the iterator, not the entry itself
    }
  }

  /**
   * @brief Looks up a key and marks it as most recently used.
   * @param[in] key Key to look up.
   * @return Copy of the value, or nullopt if the key is absent or expired.
   * @note An expired entry is reported as a miss but is left for the cleaner
   * to remove; a hit refreshes recency, not the entry's expiration.
   */
  // Returns the value for a key, or nothing if
  // it is missing/expired.
  std::optional<T> get(const std::string& key) {
    std::lock_guard<std::mutex> lock(
        mutex_);  // Lock while looking up and updating recency.
    auto item =
        cacheMap_.find(key);  // Find the key in the map; end() means absent.
    // Miss if absent or if the expiry time has
    // passed.
    if (item == cacheMap_.end() ||
        item->second->getExpiration() <= Clock::now()) {
      return std::nullopt;  // Report a miss; physical removal is the
                            // cleaner's job.
    }
    moveToFront(
        item->second);  // A hit makes this entry the most recently used.
    return item->second->getValue();  // Return a copy of the stored value.
  }

  /**
   * @brief Removes a key from the cache.
   * @param[in] key Key to remove.
   * @return True if the key was present and was removed, false otherwise.
   * @note Expiry is not checked here: an expired entry not yet cleaned is
   * still removed and reported as present.
   */
  // Deletes an entry by key, reporting
  // whether it existed.
  bool remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(
        mutex_);                      // Lock for the lookup and both erases.
    auto item = cacheMap_.find(key);  // Locate the key in the map.
    // If the key is absent, there is nothing to do.
    if (item == cacheMap_.end()) {
      return false;  // Report that no entry was removed.
    }

    cacheList_.erase(item->second);  // Erase the entry from the list using
                                     // the stored iterator.
    cacheMap_.erase(key);  // Erase the key-to-iterator entry from the map.
    return true;           // Report success.
  }

 private:
  /**
   * @brief One cache record: key, value and expiration time point.
   */
  // The data stored for each cache item.
  class CacheEntry {
   private:
    /**
     * @brief Lookup key, also used to erase the map entry during cleaning.
     */
    std::string key;  // The key is copied here so the cleaner can remove
                      // the matching map entry later.
    /**
     * @brief Cached value.
     */
    T value;  // The actual data being cached; copied on insert and returned
              // on get.
    /**
     * @brief Time after which the entry is considered expired.
     */
    TimePoint expiration;  // The moment this entry becomes stale.

   public:
    /**
     * @brief Constructs an entry with a key, value and expiration.
     * @param[in] _key Lookup key.
     * @param[in] _value Value to copy.
     * @param[in] _exp Expiration time point.
     */
    CacheEntry(std::string _key, const T& _value, TimePoint _exp)
        : key(std::move(_key)), value(_value), expiration(_exp) {
    }  // Stores all three pieces; the key is moved for efficiency.
    /**
     * @brief Returns a copy of the key.
     */
    [[nodiscard]] std::string getKey() const {
      return key;
    }  // A copy is fine because keys are small strings.
    /**
     * @brief Returns a copy of the cached value.
     */
    T getValue() const {
      return value;
    }  // Returns by value so callers get their own copy.
    /**
     * @brief Returns the expiration time point.
     */
    [[nodiscard]] TimePoint getExpiration() const {
      return expiration;
    }  // Used to decide whether the entry is stale.
    /**
     * @brief Replaces the cached value.
     * @param[in] _value New value.
     */
    void setValue(const T& _value) {
      this->value = _value;
    }  // Overwrites the stored data on a refresh.
    /**
     * @brief Replaces the expiration time point.
     * @param[in] _exp New expiration time.
     */
    void setExpiration(const TimePoint& _exp) {
      this->expiration = _exp;
    }  // Extends or changes the entry's lifetime.
  };

  /**
   * @brief Maximum number of entries retained before LRU eviction.
   */
  size_t maxSize_;  // The capacity limit; once reached, the least recently
                    // used entry is evicted.
  /**
   * @brief TTL used by insert() when no per-entry TTL is supplied.
   */
  std::chrono::seconds defaultTTL_;  // The fallback lifetime for entries
                                     // inserted without an explicit TTL.
  /**
   * @brief Key-to-list-iterator index; iterators remain valid across splices.
   */
  std::unordered_map<std::string, typename std::list<CacheEntry>::iterator>
      cacheMap_;  // Gives O(1) lookup; stores list iterators, which survive
                  // reordering.
  /**
   * @brief Entry list ordered from most recently used (front) to least
   * (back).
   */
  std::list<CacheEntry> cacheList_;  // The recency order; the back is the
                                     // next victim for eviction.
  /**
   * @brief Guards all cache state and the cleaner control flag.
   */
  std::mutex
      mutex_;  // A single lock protects the map, list, limits and stop flag.
  /**
   * @brief Wakes the cleaner early on shutdown or move.
   */
  std::condition_variable
      cleanerCv_;  // Lets the cleaner sleep until woken or a timeout passes.
  /**
   * @brief Set under mutex_ to request cleaner termination.
   */
  bool stopCleaner_;  // When true, the cleaner loop exits.
  /**
   * @brief Background thread running cleanExpiredEntries().
   */
  std::thread cleanerThread_;  // The thread that periodically removes expired
                               // entries.

  /**
   * @brief Moves an entry to the most-recently-used end of the list.
   * @param[in,out] item Iterator to the entry; the caller must hold mutex_.
   */
  void moveToFront(typename std::list<CacheEntry>::iterator& item) {
    cacheList_.splice(cacheList_.begin(), cacheList_, item);
  }  // splice relinks the node without copying; the caller holds the lock.

  /**
   * @brief Evicts the least recently used entry (the list's back).
   * @pre cacheList_ is non-empty and the caller holds mutex_.
   */
  // Removes the back (oldest/least recently used) entry.
  void evictOldest() {
    auto last = cacheList_.end();  // Start from the one-past-end iterator.
    --last;  // Step back to the last real element (the LRU entry).
    cacheMap_.erase(
        last->getKey());    // Remove its key from the lookup map first.
    cacheList_.pop_back();  // Then remove the entry itself from the list.
  }

  /**
   * @brief Cleaner loop that periodically erases all expired entries.
   * @details Waits on cleanerCv_ with a one-second timeout; a notification
   * from destruction or a move wakes it immediately so it can observe
   *          stopCleaner_ and exit. Otherwise it scans the entire list
   * because move-to-front can leave expired entries away from the back.
   */
  // The background loop run by cleanerThread_.
  void cleanExpiredEntries() {
    // Repeat until the stop flag tells us to quit.
    while (true) {
      {
        std::unique_lock<std::mutex> lock(
            mutex_);  // Lock with a unique_lock, which the condition
                      // variable can release while waiting.
        cleanerCv_.wait_for(lock, std::chrono::seconds(1), [this] {
          return stopCleaner_;
        });  // Sleep up to one second, waking early if
             // notified and the predicate is true.

        // Check whether shutdown was requested.
        if (stopCleaner_) {
          break;  // Leave the loop so the thread can finish.
        }

        auto now = Clock::now();  // Take one timestamp for this whole scan.

        // Expired entries can live anywhere after move-to-front, so
        // scan the whole list instead of only evicting from the back.
        auto it = cacheList_.begin();  // Start at the most recently used entry.
        // Walk every entry in the list.
        while (it != cacheList_.end()) {
          // If it has already expired.
          if (it->getExpiration() <= now) {
            cacheMap_.erase(it->getKey());  // Remove its key from the map.
            it = cacheList_.erase(it);      // Erase the list node and
                                            // advance to the next one.
          } else {
            ++it;  // Otherwise just move to the next entry.
          }
        }
      }
    }
  }
};
