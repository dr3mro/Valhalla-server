/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file dosdetector.cpp
 * @brief Implementation for dosdetector: Production constructor: compiles
 * configured matchers and starts the cleanup thread.
 * @details This file contains the working logic of the per-IP abuse detector.
 * The constructors turn configuration strings (regular expressions, CIDR
 * ranges, wildcards, exact IPs) into small predicate functions and launch a
 * background cleanup thread. is_dos_attack() is the main entry point: it checks
 * the static lists first, then the dynamic rate/ban state, and records each
 * request so repeat offenders can be escalated. A separate helper hashes the
 * request into a fingerprint, and several mutex-guarded maps hold the
 * timestamps and block/unblock times.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This file is the actual "bouncer" behind Filters::RateLimit. Every request
 * that reaches the rate-limit filter ends up in is_dos_attack(), which returns
 * a Status the filter turns into an allow or an HTTP error. It calls into
 * Configurator (read at construction), xxHash (to fingerprint requests) and
 * Message (for logging). Without this implementation the detector class in the
 * header would have no behaviour, and the server would be unable to detect or
 * slow down flooding and brute-force clients.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Mutex/lock_guard (automatic lock that unlocks when the scope ends):
 * https://en.cppreference.com/w/cpp/thread/lock_guard
 * - Condition variable / wait_for (sleep until woken or a timeout):
 * https://en.cppreference.com/w/cpp/thread/condition_variable
 * - std::async / std::future (a task running in the background):
 * https://en.cppreference.com/w/cpp/thread/async
 * - std::atomic (a variable safe across threads):
 * https://en.cppreference.com/w/cpp/atomic/atomic
 * - Lambda (an inline anonymous function):
 * https://en.cppreference.com/w/cpp/language/lambda
 * - xxHash XXH3 (the fast hash used to fingerprint requests):
 * https://github.com/Cyan4973/xxHash
 * - Regular expression: https://en.cppreference.com/w/cpp/regex
 * - CIDR notation (IP + network prefix):
 * https://en.wikipedia.org/wiki/Classless_Inter-Domain_Routing
 * - Rate limiting: https://en.wikipedia.org/wiki/Rate_limiting
 * - Iterator erase idiom (`it = container.erase(it)`):
 * https://en.cppreference.com/w/cpp/container/unordered_map/erase
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#include "dosdetector.hpp"  // The interface implemented here.

// fmt::format used to build digest strings and log messages.
#include <fmt/core.h>
#include <fmt/format.h>  // Additional formatting support.
#include <xxhash.h>      // XXH3 streaming API used for request fingerprints.

#include <algorithm>  // Standard algorithms (kept available for helpers).
#include <chrono>     // Time points and durations for the sliding window.
// std::condition_variable used by the cleanup thread.
#include <condition_variable>
#include <exception>  // std::exception caught throughout.
#include <future>     // std::async / std::future for the background task.
#include <mutex>      // std::lock_guard / std::unique_lock.
#include <optional>   // std::optional results that may be absent.
#include <regex>      // std::regex for "re:" whitelist/blacklist patterns.
#include <string>     // IP and fingerprint strings.
// std::chrono::seconds used when sleeping the cleanup thread.
#include <thread>
#include <unordered_map>  // The timestamp and block maps.
#include <unordered_set>  // Set types from the config.
#include <utility>        // std::move for config/clock in the test constructor.

// Message::ErrorMessage / CriticalMessage for logging failures.
#include "utils/message/message.hpp"

/**
 * @brief Production constructor: compiles configured matchers and starts the
 * cleanup thread.
 * @details Configuration comes from the Store Configurator and the real steady
 * clock is used. Exceptions during setup are logged rather than propagated,
 * which may leave the detector partially initialized (for example without a
 * cleanup thread).
 */
DOSDetector::DOSDetector()
{
  try
  {
    for (const auto& p : config_.whitelist)
      whitelist_.push_back(
          compileMatcher(p));  // Turn each whitelist pattern string into
                               // a callable matcher.
    for (const auto& p : config_.blacklist)
      blacklist_.push_back(compileMatcher(p));  // Same for the blacklist.
    async_task_clean_ =
        std::async(std::launch::async, &DOSDetector::cleanUpTask,
            this);  // Start the background pruning loop on a new thread.
  }
  catch (
      const std::exception& e)  // Pattern compilation or thread start failed.
  {
    Message::ErrorMessage(
        // Log a short user-facing error.
        "Exception during DOSDetector initialization.");
    Message::CriticalMessage(
        e.what());  // Log the underlying exception text for debugging.
  }
}

/**
 * Test/embedding constructor with injected config and clock.
 * The config is copied into owned_config_ so the config_ reference remains
 * valid independently of the caller's lifetime. config: Fully specified
 * detector configuration. clock: Clock source for all timing decisions.
 */
DOSDetector::DOSDetector(Configurator::DOSDetectorConfig config, ClockFn clock)
    : owned_config_(std::move(config)),
      config_(owned_config_.value()),
      clock_(std::move(clock))  // Own the config so the reference stays valid;
                                // take the injected clock.
{
  try
  {
    for (const auto& p : config_.whitelist)
      whitelist_.push_back(compileMatcher(p));  // Compile whitelist patterns.
    for (const auto& p : config_.blacklist)
      blacklist_.push_back(compileMatcher(p));  // Compile blacklist patterns.
    async_task_clean_ =
        std::async(std::launch::async, &DOSDetector::cleanUpTask,
            this);  // Start cleanup with the injected clock.
  }
  catch (const std::exception& e)
  {
    Message::ErrorMessage(
        // Log initialization failure.
        "Exception during DOSDetector initialization.");
    Message::CriticalMessage(e.what());  // Log details.
  }
}

/**
 * @brief Stops the cleanup loop and joins its thread before state is torn down.
 * @details The stop flag is written under clean_mutex_, then the condition
 * variable wakes the waiting loop so shutdown is prompt even with a long
 * cleanup interval. Waiting on the future guarantees no cleanup task touches
 * this object after destruction begins.
 */
DOSDetector::~DOSDetector()
{
  try
  {
    {
      // Publish the stop flag under the same mutex the waiter checks.
      std::lock_guard<std::mutex> lock(
          clean_mutex_);  // Lock so the flag change is not missed by the
                          // waiter.
      running_clean_.store(false);  // Tell the cleanup loop to stop.
    }
    clean_cv_.notify_all();  // Wake the loop immediately instead of waiting
                             // for its sleep to finish.
    if (async_task_clean_.valid())  // Only wait if we actually started a
                                    // background task.
    {
      async_task_clean_.wait();  // Block until the cleanup thread has exited;
                                 // no code touches this object afterwards.
    }
  }
  catch (const std::exception& e)
  {
    Message::ErrorMessage(
        "Exception during DOSDetector destruction.");  // Log a shutdown
                                                       // problem.
    Message::CriticalMessage(e.what());                // Log details.
  }
}

/**
 * Classifies @p request, checking static lists before dynamic abuse state.
 * Whitelisted IPs are never counted; blacklisted IPs are rejected immediately.
 *          Rate-limited IPs continue to be counted so repeated abuse can
 * escalate to a ban, while banned IPs are short-circuited with no further
 * bookkeeping. Any exception is logged and mapped to Status::ERROR. request:
 * Request snapshot to classify. Returns The applicable Status verdict.
 */
DOSDetector::Status DOSDetector::is_dos_attack(const Request& request)
{
  try
  {
    if (isWhitelisted(
            request.ip))  // First: never count or block a whitelisted IP.
    {
      return Status::WHITELISTED;  // Done - exempt from all checks.
    }
    if (isBlacklisted(request.ip))  // Second: reject a blacklisted IP outright.
    {
      return Status::BLACKLISTED;  // Done - no counting needed.
    }

    // Still record the request so a rate-limited IP can accumulate toward a
    // ban.
    if (isRateLimited(request.ip))  // Third: is this IP currently limited?
    {
      processRequest(request);     // Count it anyway, so continued abuse can
                                   // escalate to a ban.
      return Status::RATELIMITED;  // Tell the filter to answer with a
                                   // rate-limit error.
    }

    // A ban takes precedence over counting: no further state is recorded
    // while banned.
    if (isBanned(request.ip))  // Fourth: is this IP banned?
    {
      return Status::BANNED;  // Short-circuit; do not record anything
                              // while banned.
    }

    return processRequest(request);  // Otherwise count the request and return
                                     // ALLOWED / RATELIMITED / BANNED.
  }
  catch (const std::exception& e)  // Any unexpected failure while classifying.
  {
    // Log the failure.
    Message::ErrorMessage("Failure in is_dos_attack.");
    Message::CriticalMessage(e.what());  // Log details.
    return Status::ERROR;  // Fail safe: signal an internal error to the
                           // filter.
  }
}
/**
 * @brief Prunes request timestamps older than @p window from all per-IP
 * fingerprint queues.
 * @details Runs under request_mutex_. Fingerprints with no remaining timestamps
 * are erased so the distinct-fingerprint count cannot grow without bound.
 * @param[in] window Cutoff time; entries strictly older than it are discarded.
 */
inline void __attribute((always_inline)) DOSDetector::clean_requests(
    const std::chrono::time_point<std::chrono::steady_clock>&
        window)  // Cleanup requests and blocked IPs
{
  std::lock_guard<std::mutex> request_lock(
      request_mutex_);  // Protect the nested request maps for the whole
                        // sweep.
  // Cleanup requests
  for (auto& request : requests_)  // Visit every IP's bucket of fingerprints.
  {
    auto& requests = request.second;  // The map of fingerprint -> timestamp
                                      // queue for this IP.

    for (auto it = requests.begin(); it != requests.end();
        /* no increment here */)  // Manual iterator walk: erase() changes
                                  // the iterator, so we advance it
                                  // ourselves below.
    {
      auto& times =
          it->second;  // The queue of arrival times for this fingerprint.

      // Timestamps are appended in time order, so only the front needs
      // checking.
      while (
          !times.empty() &&
          times.front() < window)  // Pop all timestamps older than the cutoff.
      {
        times.pop_front();  // Remove the oldest timestamp.
      }

      // Drop the fingerprint bucket entirely once it becomes empty.
      if (times.empty())  // No timestamps left for this fingerprint.
      {
        it = requests.erase(it);  // Erase returns the next iterator, so
                                  // the loop keeps its place.
      }
      else
      {
        ++it;  // Otherwise advance normally.
      }
    }
  }
}
/**
 * @brief Removes rate-limit entries whose unblock time has been reached.
 * @details Runs under ratelimit_mutex_; map values are the absolute unblock
 * times.
 * @param[in] now Current time from the detector clock.
 */
inline void __attribute((always_inline)) DOSDetector::clean_ratelimited_ips(
    const std::chrono::time_point<std::chrono::steady_clock>& now)
{
  std::lock_guard<std::mutex> block_lock(
      ratelimit_mutex_);  // Protect the rate-limit map.
  for (auto it = ratelimited_ips_.begin(); it != ratelimited_ips_.end();
      /* no increment here */)  // Manual walk so erase() can be used safely.
  {
    if (now >= it->second)  // The stored unblock time has passed.
    {
      // fmt::print("Removing ratelimited IP : {}\n", it->first);
      it = ratelimited_ips_.erase(
          it);  // Unblock IP  // Remove the entry and advance.
    }
    else
    {
      ++it;  // Still blocked; keep the entry and advance.
    }
  }
}
/**
 * @brief Removes ban entries whose unban time has been reached.
 * @details Runs under ban_mutex_; map values are the absolute unban times.
 * @param[in] now Current time from the detector clock.
 */
inline void __attribute((always_inline)) DOSDetector::clean_banned_ips(
    const std::chrono::time_point<std::chrono::steady_clock>& now)
{
  std::lock_guard<std::mutex> ban_lock(ban_mutex_);  // Protect the ban map.
  for (auto it = banned_ips_.begin(); it != banned_ips_.end();
      /* no increment here */)  // Manual walk so erase() can be used safely.
  {
    if (now >= it->second)  // The unban time has passed.
    {
      it = banned_ips_.erase(it);  // Unblock IP  // Remove the ban and advance.
    }
    else
    {
      ++it;  // Still banned; advance.
    }
  }
}
/**
 * @brief Background loop that periodically prunes expired requests and block
 * entries.
 * @details Uses the injected clock to compute the cutoff window but waits in
 * real time via condition_variable::wait_for, so an injected test clock cannot
 * stall shutdown. The stop flag is rechecked each iteration.
 */
void DOSDetector::cleanUpTask()
{
  try
  {
    // Atomic stop flag is read outside the mutex; the destructor sets it
    // under clean_mutex_.
    while (running_clean_
            .load())  // Keep sweeping until the destructor signals stop.
    {
      auto now = clock_();  // Current time from the (possibly injected) clock.
      auto window = now - config_.period;  // Anything older than this is
                                           // outside the counting window.

      clean_requests(window);      // clean up requests older than time window
      clean_ratelimited_ips(now);  // clean up ratelimited ips [time stored in
      // ratelimited_ips_ equals time to unblock]
      clean_banned_ips(now);  // clean up banned ips [time stored in
      // banned_ips_ equals time to unblock]

      // Sleep for the cleanup frequency (real time), but wake up
      // immediately when the destructor asks us to stop. Equivalent to
      // the previous sleep_until(now + clean_freq) for the real clock,
      // and safe with an injected (test) clock.
      std::unique_lock<std::mutex> lock(
          clean_mutex_);  // Lock required by
                          // condition_variable::wait_for.
      clean_cv_.wait_for(lock, std::chrono::seconds(config_.clean_freq),
          [this]
          {
            return !running_clean_.load();
          });  // Sleep up to clean_freq seconds, or wake early when
               // stopping; the predicate guards against spurious wakeups.
    }
  }
  catch (const std::exception& e)
  {
    Message::ErrorMessage(
        "Exception in cleanUpTask.");    // Log a cleanup failure.
    Message::CriticalMessage(e.what());  // Log details.
  }
}

/**
 * @brief Hashes the request's header hash and body into a reusable fingerprint
 * string.
 * @details The header hash (already combined by hashHeaders) and the body are
 * fed into a streaming XXH3 digest. The fingerprint identifies "the same
 * request" for repeat counting; an empty body still yields a digest of the
 * header hash alone.
 * @param[in] req Request whose header_hash and body are consumed.
 * @return Formatted digest, or std::nullopt if hashing throws.
 */
inline std::optional<std::string> __attribute((always_inline))
DOSDetector::generateRequestFingerprint(const DOSDetector::Request& req)
{
  try
  {
    std::uint64_t header_hash = req.header_hash;  // Copy the 64-bit header
                                                  // fingerprint into a local.

    XXH3_state_t* state =
        XXH3_createState();    // Allocate a streaming hash state.
    XXH3_64bits_reset(state);  // Initialize it for a new digest.
    XXH3_64bits_update(state, &header_hash,
        sizeof(header_hash));  // Feed in the header hash bytes.
    if (!req.body.empty())     // Only feed the body if there is one.
    {
      XXH3_64bits_update(state, req.body.data(),
          req.body.size());  // Add the body bytes to the digest.
    }
    XXH64_hash_t hashed_key = XXH3_64bits_digest(
        state);             // Finish the digest and get the 64-bit result.
    XXH3_freeState(state);  // Release the hash state (no leak).

    return fmt::format("{}",
        hashed_key);  // Turn the number into a string so it can be a map
                      // key.
  }
  catch (const std::exception& e)
  {
    Message::ErrorMessage(
        "Exception in generateRequestFingerprint.");  // Log hashing
                                                      // failure.
    Message::CriticalMessage(e.what());               // Log details.
    return std::nullopt;  // Signal that no fingerprint could be made.
  }
}

/**
 * @brief Compiles a configuration pattern string into an IP predicate.
 * @details Supports four forms, checked in order: `re:` regex
 * (std::regex_search), IPv4 CIDR, dot-separated `*` wildcards, and exact match.
 * IPv4 CIDR with an invalid prefix or non-IPv4 network falls back to a matcher
 * that never matches. Regex and stoi parsing may throw; this is called from the
 * constructors' try blocks.
 * @param[in] pattern Pattern text from the whitelist or blacklist
 * configuration.
 * @return Callable that tests a single IP string.
 */
DOSDetector::Matcher DOSDetector::compileMatcher(
    const std::string& pattern) const
{
  // Regex form uses search, so an unanchored pattern can match a substring.
  if (pattern.rfind("re:", 0) == 0)  // Does the pattern start with "re:"?
  {
    std::regex re(
        pattern.substr(3));  // Compile the rest as a regular expression.
    return [re](const std::string& ip)
    {
      return std::regex_search(ip, re);
    };  // Matcher that searches the IP for that regex.
  }
  if (pattern.find('/') !=
      std::string::npos)  // A slash means CIDR notation (network/prefix).
  {
    // CIDR
    std::string net = pattern.substr(
        0, pattern.find('/'));  // The IP/network part before the slash.
    int          prefix = std::stoi(pattern.substr(
        pattern.find('/') + 1));  // The numeric prefix length after the slash.
    // parse IPv4
    unsigned int a, b, c, d;  // Four octets of an IPv4 address.
    if (sscanf(net.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) ==
        4)  // Did we read exactly four numbers?
    {
      uint32_t network = (a << 24) | (b << 16) | (c << 8) |
                         d;  // Pack the four octets into one 32-bit number.
      uint32_t mask =
          prefix == 0
              ? 0
              : (0xFFFFFFFFu << (32 - prefix));  // Build the network mask;
                                                 // /0 matches everything.
      network &= mask;  // Normalize the network address by clearing host bits.
      return [network, mask](const std::string& ip)
      {
        unsigned int a2, b2, c2, d2;  // Octets of the IP being tested.
        if (sscanf(ip.c_str(), "%u.%u.%u.%u", &a2, &b2, &c2, &d2) != 4)
          return false;  // Not IPv4 -> no match.
        uint32_t val = (a2 << 24) | (b2 << 16) | (c2 << 8) |
                       d2;               // Pack the test IP into 32 bits.
        return (val & mask) == network;  // In the same subnet if masked
                                         // bits equal the network.
      };
    }
    return [](const std::string&)
    { return false; };  // Malformed CIDR -> matcher that never matches.
  }
  // Wildcard form matches dot-separated segments; '*' accepts any one
  // segment.
  if (pattern.find('*') !=
      std::string::npos)  // Does the pattern contain a '*'?
  {
    std::vector<std::string> parts;  // Will hold the pattern split on dots.
    std::string              cur;    // Accumulates the current segment.
    for (char ch : pattern)          // Walk each character of the pattern.
    {
      if (ch == '.')  // Dot ends the current segment.
      {
        parts.push_back(cur);  // Store the finished segment.
        cur.clear();           // Start a new segment.
      }
      else
      {
        cur.push_back(ch);  // Build the current segment.
      }
    }
    parts.push_back(cur);  // Add the final segment (no trailing dot).
    return [parts](const std::string& ip)
    {
      std::vector<std::string> ipParts;  // The test IP split on dots.
      std::string              cur2;     // Current segment of the test IP.
      for (char ch : ip)                 // Walk each character of the test IP.
      {
        if (ch == '.')  // Dot ends the current segment.
        {
          ipParts.push_back(cur2);  // Store the finished segment.
          cur2.clear();             // Start a new segment.
        }
        else
        {
          cur2.push_back(ch);  // Build the current segment.
        }
      }
      ipParts.push_back(cur2);  // Add the final segment.
      if (ipParts.size() != parts.size())
        return false;  // Different number of segments -> no match.
      for (size_t i = 0; i < parts.size(); ++i)  // Compare each segment.
      {
        if (parts[i] != "*" && parts[i] != ipParts[i])
          return false;  // A non-'*' segment must match exactly.
      }
      return true;  // Every segment matched.
    };
  }
  // exact
  std::string exact =
      pattern;  // Copy so the lambda owns the string it compares against.
  return [exact](const std::string& ip)
  { return ip == exact; };  // Exact string match.
}

/**
 * @brief True if any compiled whitelist matcher accepts @p remote_ip.
 * @details Exceptions from matchers are caught and treated as "not
 * whitelisted".
 */
inline bool __attribute((always_inline)) DOSDetector::isWhitelisted(
    const std::string& remote_ip)
{
  try
  {
    for (const auto& m : whitelist_)
      if (m(remote_ip))
        return true;  // First matcher that accepts means whitelisted.
    return false;     // No matcher accepted.
  }
  catch (const std::exception& e)
  {
    Message::ErrorMessage(
        "Exception in isWhitelisted");   // Log matcher failure.
    Message::CriticalMessage(e.what());  // Log details.
    return false;  // Fail closed: treat as not whitelisted.
  }
}

/**
 * @brief True if any compiled blacklist matcher accepts @p remote_ip.
 * @details Exceptions from matchers are caught and treated as "not
 * blacklisted".
 */
inline bool __attribute((always_inline)) DOSDetector::isBlacklisted(
    const std::string& remote_ip)
{
  try
  {
    for (const auto& m : blacklist_)
      if (m(remote_ip))
        return true;  // First accepting matcher means blacklisted.
    return false;     // No matcher accepted.
  }
  catch (const std::exception& e)
  {
    Message::ErrorMessage(
        "Exception in isBlacklisted.");  // Log matcher failure.
    Message::CriticalMessage(e.what());  // Log details.
    return false;  // Fail closed: do not block on an internal error.
  }
}

/**
 * @brief True if @p remote_ip has a live ban entry.
 * @details Delegates to checkStatus(); exceptions yield false.
 */
inline bool __attribute((always_inline)) DOSDetector::isBanned(
    const std::string& remote_ip)
{
  try
  {
    return checkStatus(remote_ip, banned_ips_,
        ban_mutex_);  // Ask the shared helper with the ban map and its
                      // lock.
  }
  catch (const std::exception& e)
  {
    Message::ErrorMessage("Exception in isBanned.");  // Log lookup failure.
    Message::CriticalMessage(e.what());               // Log details.

    return false;  // Fail closed: do not treat an error as a ban.
  }
}

/**
 * @brief True if @p remote_ip has a live rate-limit entry.
 * @details Delegates to checkStatus(); exceptions yield false.
 */
inline bool __attribute((always_inline)) DOSDetector::isRateLimited(
    const std::string& remote_ip)
{
  try
  {
    return checkStatus(remote_ip, ratelimited_ips_,
        ratelimit_mutex_);  // Use the shared helper with the rate-limit map
                            // and lock.
  }
  catch (const std::exception& e)
  {
    Message::ErrorMessage(
        fmt::format("Exception in isRateLimited."));  // Log lookup failure.
    Message::CriticalMessage(e.what());               // Log details.

    return false;  // Fail closed.
  }
}

/**
 * Tests an IP against an absolute-expiry map, lazily erasing stale entries.
 * Map values are instants in time; an entry is "active" while `now <
 * forget_time`. The caller passes the mutex that guards @p ip_map. Template
 * parameter Map: Map from IP string to a comparable time point. Template
 * parameter Mutex: Mutex type protecting @p ip_map. remote_ip: IP to look up.
 * ip_map: Block/rate-limit state map.
 * mtx: Mutex locked for the duration of the lookup.
 * Returns True while the IP is still blocked; false otherwise (including on
 * exception).
 */
template <typename Map, typename Mutex>
inline bool __attribute((always_inline)) DOSDetector::checkStatus(
    const std::string& remote_ip, Map& ip_map, Mutex& mtx)
{
  try
  {
    auto now = clock_();  // Current time from the detector clock.
    std::lock_guard<Mutex> lock(
        mtx);  // Lock the caller-supplied mutex for this whole lookup.

    if (ip_map.find(remote_ip) != ip_map.end())  // Is the IP in the map at all?
    {
      auto forget_time =
          ip_map.at(remote_ip);  // The time when its block expires.
      // Still blocked: keep the entry until the cleanup thread or this
      // lazy erase removes it.
      if (now < forget_time)  // Before expiry -> still active.
      {
        return true;  // Report blocked.
      }

      ip_map.erase(remote_ip);  // Expired: remove it now (lazy cleanup)
                                // and fall through.
    }
    return false;  // No entry (or it was expired): not blocked.
  }
  catch (const std::exception& e)
  {
    Message::ErrorMessage("Exception in checkStatus.");  // Log lookup failure.
    Message::CriticalMessage(e.what());                  // Log details.

    return false;  // Fail closed on error.
  }
}

/**
 * @brief Records a request and decides whether to rate-limit, ban or allow it.
 * @details Under request_mutex_ it prunes timestamps outside the configured
 * period, appends the current time to the per-IP/per-fingerprint queue, then
 * applies two thresholds: too many distinct fingerprints yields RATELIMITED;
 * too many repeats of one fingerprint yields BANNED. Rate-limit and ban state
 * are written under their own mutexes. The distinct-fingerprint check runs
 * first, so an IP can be rate-limited before it is eventually banned.
 * @param[in] request Request to record.
 * @return ALLOWED, RATELIMITED, BANNED, or ERROR on internal failure.
 */
DOSDetector::Status DOSDetector::processRequest(const Request& request)
{
  try
  {
    auto                       now = clock_();  // Timestamp for this request.
    std::optional<std::string> remote_ip =
        request.ip;  // IP of the caller (kept optional for safety checks
                     // below).
    std::optional<std::string> request_fingerprint =
        generateRequestFingerprint(request);  // Hash of headers+body.

    if (!remote_ip)  // Defensive: if there were no IP we could not key the
                     // counters.
    {
      Message::ErrorMessage(
          "Failed to get ip from request.");  // Log the problem.
      return Status::ERROR;                   // Cannot classify.
    }
    // fmt::print("Request from {}\n", remote_ip.value());
    if (!request_fingerprint)  // Hashing failed.
    {
      Message::ErrorMessage(
          // Log the problem.
          "Failed to generate request fingerprint.");
      return Status::ERROR;  // Cannot classify.
    }
    // fmt::print("Request fingerprint: {}\n", request_fingerprint.value());

    {
      // fmt::print("Checking request from {}\n", remote_ip.value());
      std::lock_guard<std::mutex> request_lock(
          request_mutex_);  // Protect the nested request maps while we
                            // read and update them.

      // fmt::print("get the requests for the ip: {}\n",
      // remote_ip.value());
      auto& ip_requests =
          requests_[remote_ip.value()];  // Get (or create) this IP's map
                                         // of fingerprints.

      // fmt::print("get the fingerprint for the ip: {} :: {}\n",
      // remote_ip.value(), request_fingerprint.value());
      auto& fp_requests =
          ip_requests[request_fingerprint.value()];  // Get (or create)
                                                     // the queue for this
                                                     // fingerprint.

      // Only the trailing window matters for both abuse thresholds.
      const auto window =
          now - config_.period;  // Cutoff for "recent" requests.

      // Remove old requests that are outside the time window
      while (!fp_requests.empty() &&
             fp_requests.front() <
                 window)  // Drop timestamps older than the window.
      {
        fp_requests.pop_front();  // Remove the oldest timestamp.
        // fmt::print("Removing old request from {}\n",
        // remote_ip.value());
      }

      fp_requests.push_back(now);  // Record this request's arrival time.

      // fmt::print("size: fp :{} r:{}\n", fp_requests.size(),
      // ip_requests.size());

      // Distinct fingerprints first: a burst of varying requests is
      // rate-limited.
      if (ip_requests.size() >
          config_.max_fingerprints)  // Too many different request shapes
                                     // from this IP.
      {
        std::lock_guard<std::mutex> block_lock(
            ratelimit_mutex_);  // Lock the rate-limit map (nested
                                // inside request_mutex_).
        ratelimited_ips_[remote_ip.value()] =
            now + config_.ratelimit_duration;  // Block until now +
                                               // configured duration.
        return Status::RATELIMITED;            // Tell the filter to rate-limit
                                               // this IP.
      }

      // Identical repeats are a stronger abuse signal and escalate to a
      // ban.
      if (fp_requests.size() >
          config_.max_requests)  // Too many repeats of this exact request.
      {
        std::lock_guard<std::mutex> ban_lock(ban_mutex_);  // Lock the ban map.
        banned_ips_[remote_ip.value()] =
            now + config_.ban_duration;  // Ban until now + configured duration.
        return Status::BANNED;           // Tell the filter to reject this IP.
      }
      // fmt::print("request from {}: {}\n", remote_ip.value(),
      // request_fingerprint.value());
    }
  }
  catch (const std::exception& e)
  {
    Message::ErrorMessage(
        "Failure in processRequest.");   // Log unexpected failure.
    Message::CriticalMessage(e.what());  // Log details.
    return Status::ERROR;                // Fail safe.
  }
  return Status::ALLOWED;  // No threshold exceeded: the request may proceed.
}
