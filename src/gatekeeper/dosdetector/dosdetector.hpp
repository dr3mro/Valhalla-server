/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file dosdetector.hpp
 * @brief Per-IP abuse detector combining static lists with sliding-window
 * request counting.
 * @details This header declares DOSDetector, a class that decides whether an
 * incoming request should be allowed, is on a whitelist/blacklist, or is part
 * of abusive traffic. It counts recent requests per IP address and per
 * "fingerprint" (a hash of the request's headers and body) inside a moving time
 * window. Too many different fingerprints means "rate limited"; too many
 * repeats of one fingerprint means "banned". A background thread periodically
 * removes counters and blocks that have expired.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the defence against denial-of-service and brute-force traffic. A
 * Drogon filter (Filters::RateLimit) captures each request and calls
 * GateKeeper::isDosAttack, which forwards to this detector's is_dos_attack().
 * Based on the returned Status the filter either lets the request through or
 * sends an error. The detector is one shared object in Store, so all request
 * threads see the same counters. Without it the server would have no way to
 * notice or slow an attacker flooding it with requests.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Mutex/lock (protecting shared data from simultaneous threads):
 * https://en.cppreference.com/w/cpp/thread/mutex
 * - Condition variable (a thread sleeping until told to wake):
 * https://en.cppreference.com/w/cpp/thread/condition_variable
 * - std::async / std::future (running a task in the background):
 * https://en.cppreference.com/w/cpp/thread/async
 * - std::atomic (a variable safe to read/write from many threads):
 * https://en.cppreference.com/w/cpp/atomic/atomic
 * - Sliding window / rate limiting: https://en.wikipedia.org/wiki/Rate_limiting
 * - Denial-of-service attack:
 * https://en.wikipedia.org/wiki/Denial-of-service_attack
 * - Hash function (turning data into a fixed-size number):
 * https://en.wikipedia.org/wiki/Hash_function
 * - xxHash (the fast hash used here): https://github.com/Cyan4973/xxHash
 * - Regular expression: https://en.cppreference.com/w/cpp/regex
 * - CIDR notation (IP address + network prefix):
 * https://en.wikipedia.org/wiki/Classless_Inter-Domain_Routing
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include once per translation unit.

#include <xxhash.h>  // XXH3_64bits, the fast hash used to fingerprint requests.

// For std::atomic<bool> running_clean_, the thread stop flag.
#include <atomic>
#include <chrono>  // For time points and durations used by the sliding window.
// For clean_cv_, which lets the destructor wake the cleanup thread.
#include <condition_variable>
#include <cstdint>  // For uint8_t / uint64_t fixed-width numbers.
// For std::deque, the timestamp queues (fast push/pop at both ends).
#include <deque>
// For std::function, used by Matcher and the injectable ClockFn.
#include <functional>
// For std::future, the handle to the background cleanup task.
#include <future>
#include <memory>       // For std::shared_ptr to the Configurator.
#include <mutex>        // For the mutexes guarding each map.
#include <optional>     // For std::optional results that may be absent.
#include <string>       // For IP strings and fingerprint strings.
#include <string_view>  // For non-owning views of method/path/body in Request.
// For the maps from IP -> fingerprint -> timestamps and block maps.
#include <unordered_map>
// For storing whitelist/blacklist entries in the config.
#include <unordered_set>

// Configurator::DOSDetectorConfig, the thresholds used here.
#include "configurator/configurator.hpp"
// Store::getObject<Configurator>() in the default constructor and member
// initializer.
#include "store/store.hpp"

/**
 * @class DOSDetector
 * @brief Per-IP abuse detector combining static lists with sliding-window
 * request counting.
 * @details Instances classify requests as whitelisted, blacklisted,
 * rate-limited, banned or allowed. State is kept in several maps, each guarded
 * by its own mutex, and a background cleanup thread periodically prunes expired
 * timestamps and block entries. The detector is shared process-wide through
 * Store and consulted by the RateLimit filter via GateKeeper::isDosAttack.
 * Configuration and the clock are injectable for deterministic tests without
 * changing production behaviour.
 * @note Thread-safety: per-map mutexes serialize access to the corresponding
 * state; matcher lists are compiled once at construction and are thereafter
 * immutable. The detector is non-copyable and non-movable because it owns a
 * background thread and mutexes.
 * @see GateKeeper::isDosAttack, Filters::RateLimit
 * @details For a beginner: imagine a bouncer with three lists. The whitelist
 * never gets counted, the blacklist is refused immediately, and everyone else
 * is counted. If an IP sends many *different* requests quickly it is
 * rate-limited; if it repeats the *same* request too often it is banned.
 * Separate locks protect separate lists so different threads can check
 * different lists at the same time. The "injectable clock" simply means tests
 * can supply a fake time source instead of the real one.
 */
class DOSDetector
{
 public:
  /** @brief Verdict returned for a classified request. */
  enum Status : uint8_t  // Stored as an 8-bit number because there are only
                         // six verdicts.
  {
    ALLOWED,     /**< No rule matched; request may proceed. */
    WHITELISTED, /**< IP matched the whitelist; never counted or blocked. */
    BLACKLISTED, /**< IP matched the blacklist; rejected outright. */
    RATELIMITED, /**< Too many distinct request fingerprints in the period.
                  */
    BANNED,      /**< One fingerprint repeated too often in the period. */
    ERROR /**< Internal failure while classifying; fail-safe error response.
           */
  };
  /** @brief Lightweight view of a request supplied by the HTTP filter. */
  using Request = struct Request
  {
    std::string ip; /**< Peer IP used for list matching and per-IP counters.
                       Owned string so the detector can keep it as a map key. */
    std::string_view
        method; /**< HTTP method (captured for completeness; not used by
                   detection). A view avoids copying; currently unused. */
    std::string_view
        path; /**< Request path (captured for completeness; not used by
                 detection). A view avoids copying; currently unused. */
    std::uint64_t
        header_hash; /**< Hash of header values, used in the request
                        fingerprint. A 64-bit hash keeps collisions rare. */
    std::string_view
        body; /**< Request body, hashed together with the header hash. A
                 view so the filter need not copy the body. */
  };

  /**
   * @brief Hashes a range of key/value pairs into a single header
   * fingerprint.
   * @details Only the values are hashed, and the per-value XXH3 digests are
   * combined with XOR, so the result is independent of insertion order and of
   * header names. Callers must keep the underlying value storage alive for
   * the duration of the call.
   * @tparam Range Range of pairs whose second element is convertible to a
   * byte span.
   * @param[in] headers Header collection (for example Drogon's request
   * headers).
   * @return Combined 64-bit hash (zero for an empty range).
   * @details XOR is used to combine the digests because a XOR b equals b XOR
   * a; that makes the result order-independent, which is useful because HTTP
   * header order is not meaningful. It also means duplicate headers cancel
   * each other out.
   */
  template <typename Range>
  static std::uint64_t hashHeaders(const Range& headers)
  {
    std::uint64_t h = 0;  // Start at zero; XOR-ing values into zero just
                          // yields those values.
    for (const auto& kv : headers)  // Visit every header in the collection.
    {
      h ^= XXH3_64bits(kv.second.data(),
                       kv.second.size());  // Hash only the value (kv.second)
                                           // and fold it in with XOR.
    }
    return h;  // The combined fingerprint (zero if there were no headers).
  }

  /**
   * Injectable clock (test seam). Returns a steady_clock time point.
   */
  using ClockFn = std::function<std::chrono::time_point<
      std::chrono::steady_clock>()>;  // A callable returning "now";
                                      // steady_clock never goes backwards.

  /**
   * @brief Production constructor: loads configuration from the Store
   * Configurator.
   * @details Compiles the whitelist/blacklist patterns and starts the
   * background cleanup thread using the real steady clock.
   */
  DOSDetector();
  /**
   * @brief Test/embedding constructor taking an explicit config and clock.
   * @details Test seam: fully specified config + clock. Production behaviour
   * is unchanged; the default constructor still reads the Store Configurator
   *          and uses the real steady clock. Owns the supplied config for the
   *          detector's lifetime so `config_` stays valid.
   * @param[in] config Fully specified detector configuration.
   * @param[in] clock Clock source used for all time comparisons and cleanup
   * scheduling.
   */
  DOSDetector(Configurator::DOSDetectorConfig config, ClockFn clock);
  DOSDetector(const DOSDetector&) = delete;  // No copy: it owns a thread and
                                             // mutexes, which cannot be copied.
  DOSDetector(DOSDetector&&)      = delete;  // No move for the same reason.
  DOSDetector& operator=(const DOSDetector&) = delete;  // No copy assignment.
  DOSDetector& operator=(DOSDetector&&)      = delete;  // No move assignment.
  /**
   * @brief Stops and joins the background cleanup thread before releasing
   * state.
   */
  virtual ~DOSDetector();  // Virtual so it can be safely destroyed through a
                           // base pointer.

  /**
   * @brief Classifies a request against whitelist, blacklist and abuse
   * counters.
   * @details Evaluation order: whitelist, blacklist, rate-limit, ban, then
   * counting. A rate-limited IP is still recorded via processRequest() so it
   * can escalate to a ban. Any exception is caught and yields Status::ERROR.
   * @param[in] request Request snapshot whose IP and header/body fingerprint
   * are examined.
   * @return The applicable Status verdict.
   */
  DOSDetector::Status is_dos_attack(const Request& request);

 private:
  std::shared_ptr<Configurator> configurator_ =
      Store::getObject<Configurator>(); /**< Configuration source used by the
                                           default constructor; also keeps the
                                           Configurator alive. */
  std::optional<Configurator::DOSDetectorConfig>
      owned_config_; /**< Set by the test seam constructor to give config_ a
                        stable address (optional because production uses the
                        Store copy). */
  const Configurator::DOSDetectorConfig& config_ = configurator_->get<
      Configurator::DOSDetectorConfig>(); /**< Effective config; aliases
                                             either the Store or
                                             owned_config_. A reference avoids
                                             copying the settings. */
  ClockFn clock_ = []
  {
    return std::chrono::steady_clock::now();
  }; /**< Time source (real clock in production, injected in tests). Default
        lambda returns the real time. */
  static constexpr int REQUEST_BUFFER_SIZE =
      4096; /**< Declared but unused; no original purpose is recorded.
               constexpr means it is a compile-time constant. */
  /** @brief Drops request timestamps older than the sliding window under
   * request_mutex_. */
  inline void __attribute((always_inline)) clean_requests(
      const std::chrono::time_point<std::chrono::steady_clock>&
          window);  // always_inline hints the compiler to splice this small
                    // helper into its caller.
  /** @brief Erases rate-limit entries whose unblock time has passed. */
  inline void __attribute((always_inline)) clean_ratelimited_ips(
      const std::chrono::time_point<std::chrono::steady_clock>& now);
  /** @brief Erases ban entries whose unban time has passed. */
  inline void __attribute((always_inline)) clean_banned_ips(
      const std::chrono::time_point<std::chrono::steady_clock>& now);

  //                     // IP                       // Hash of Request //
  //                     times of requests
  std::unordered_map<
      std::string,
      std::unordered_map<std::string,
                         std::deque<std::chrono::steady_clock::time_point>>>
      requests_; /**< Per-IP, per-fingerprint arrival timestamps. Nested: IP
                    -> fingerprint -> queue of arrival times. */
  std::unordered_map<std::string, std::chrono::steady_clock::time_point>
      ratelimited_ips_;  // time to unblock  // Maps a rate-limited IP to the
                         // time when it may be unblocked.
  std::unordered_map<std::string, std::chrono::steady_clock::time_point>
      banned_ips_;  // time to unban     // Maps a banned IP to the time when
                    // its ban ends.
  using Matcher = std::function<bool(
      const std::string&)>; /**< Compiled predicate matching one IP pattern. A
                               function that answers "does this IP match?". */
  std::vector<Matcher>
      whitelist_; /**< Never-counted IPs, evaluated first. Compiled once at
                     construction, then read-only. */
  std::vector<Matcher>
      blacklist_; /**< Always-rejected IPs, evaluated before rate/ban state.
                     Also compiled once and read-only. */

  std::mutex request_mutex_; /**< Guards requests_. Request threads must lock
                                this before touching the timestamp maps. */
  std::mutex
      ratelimit_mutex_;  /**< Guards ratelimited_ips_. Separate lock so
                            checking bans need not wait on request counting. */
  std::mutex ban_mutex_; /**< Guards banned_ips_. Separate lock for the same
                            reason. */
  // (no mutexes: matchers are compiled once at construction and then
  // immutable) Because the matcher lists never change after construction,
  // reading them from many threads at once is safe without a lock.

  std::future<void> async_task_clean_; /**< Handle for the background cleanup
                                          thread, joined in the destructor. */
  std::atomic<bool> running_clean_{
      true}; /**< Shared stop flag for the cleanup loop. Atomic so the
                destructor and loop can touch it without a data race. */
  std::mutex clean_mutex_;  // guards the cleanup-thread wait  // Protects the
                            // wait so notify_all cannot be missed.
  std::condition_variable clean_cv_;  // lets the destructor wake the cleanup
                                      // thread promptly  // Used with wait_for
                                      // to sleep, and notify_all to wake early.

  /** @brief Background loop: prunes expired state until the destructor
   * signals shutdown. */
  void cleanUpTask();  // The function run by the background std::async task.
  /**
   * @brief Computes the request fingerprint from header hash and body.
   * @return Hex digest string, or std::nullopt if hashing throws.
   */
  inline std::optional<std::string> __attribute((always_inline))
  generateRequestFingerprint(
      const DOSDetector::Request&
          request);  // A string lets the fingerprint be used as a map key.
  /** @brief True if any whitelist matcher matches @p remote_ip. */
  inline bool __attribute((always_inline)) isWhitelisted(
      const std::string& remote_ip);
  /** @brief True if any blacklist matcher matches @p remote_ip. */
  inline bool __attribute((always_inline)) isBlacklisted(
      const std::string& remote_ip);
  /**
   * @brief Compiles a configuration pattern into an IP matcher.
   * @details Recognizes `re:` regular expressions, IPv4 CIDR notation, `*`
   * dot-segment wildcards and exact strings, in that priority order. A
   * malformed CIDR yields a matcher that never matches.
   */
  Matcher compileMatcher(const std::string& pattern) const;
  /** @brief True if @p remote_ip currently has a live ban entry. */
  inline bool __attribute((always_inline)) isBanned(
      const std::string& remote_ip);
  /** @brief True if @p remote_ip currently has a live rate-limit entry. */
  inline bool __attribute((always_inline)) isRateLimited(
      const std::string& remote_ip);
  /**
   * @brief Reports whether @p remote_ip has a non-expired entry in @p ip_map.
   * @details On seeing an expired entry it erases it before returning false,
   * so status checks also perform lazy cleanup. The caller supplies the
   * matching mutex.
   */
  template <typename Map, typename Mutex>
  inline bool __attribute((always_inline)) checkStatus(
      const std::string& remote_ip,
      Map&               ip_map,
      Mutex& mtx);  // Works for both ban and rate-limit maps; the mutex type
                    // is a template parameter.

  /**
   * @brief Records a request and applies the fingerprint-count thresholds.
   * @details Prunes timestamps outside the period, appends the current time,
   * then checks distinct fingerprints first (rate-limit) and repeat count
   * second (ban).
   */
  Status processRequest(const Request& request);
};
