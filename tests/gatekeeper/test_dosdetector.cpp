/**
 * @file test_dosdetector.cpp
 * @brief Catch2 unit tests and fixtures for dosdetector. Checks how the
 * detector classifies requests as allowed, whitelisted, blacklisted,
 * rate-limited, or banned.
 * @details The detector tracks request fingerprints (IP, method, path, header
 * hash and body) inside a sliding time window; a fake clock lets the time-based
 * rules be tested instantly instead of by sleeping. The tests pin down
 * repeat-count bans, distinct-fingerprint rate limits, expiry of bans and rate
 * limits, whitelist/blacklist precedence (including regex and malformed
 * patterns), anchored whitelist matching, window rollover, independence from
 * the background cleaner, and safe use by concurrent callers.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It protects dosdetector.hpp, the component that shields the API from abusive
 * clients through GateKeeper::isDosAttack. Without these tests a small logic
 * slip could ban legitimate users, let attackers through, deadlock under
 * concurrent traffic, or crash on a bad regex supplied in configuration.
 * Because the rules combine several thresholds and two notions of time,
 * regressions here are easy to introduce and hard to notice by hand.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE / SECTION (sections re-run the test body with different
 * setup):
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Catch2 CHECK / REQUIRE / REQUIRE_FALSE / CAPTURE assertion helpers:
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - std::chrono clocks, durations and time points (the fake clock mimics
 * these): https://en.cppreference.com/w/cpp/chrono
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#include <atomic>  // std::atomic counters used by the concurrent-callers test.
// Provides TEST_CASE, SECTION, CHECK, REQUIRE and CAPTURE.
#include <catch2/catch_test_macros.hpp>
// Provides data-driven test helpers (GENERATE / CAPTURE).
#include <catch2/generators/catch_generators.hpp>
// Seconds and time points used for the detector's windows and the fake clock.
#include <chrono>
// std::string for IPs, bodies and whitelist/blacklist entries.
#include <string>
#include <thread>  // std::thread for the multithreaded safety test.
// The whitelist and blacklist are stored as sets of strings.
#include <unordered_set>
// std::vector holds pre-built request bodies and worker threads.
#include <vector>

// Configurator::DOSDetectorConfig, the tunable thresholds under test.
#include "configurator/configurator.hpp"
// Shared test helpers (not needed directly here, kept for consistency).
#include "support/builders.hpp"
// FakeClock, the controllable time source injected into the detector.
#include "support/fake_clock.hpp"

using DOSDetector = ::DOSDetector;  // Alias that makes the global detector type
                                    // shorter to refer to below.

namespace
{
  /**
   * Builds a DOSDetectorConfig with explicit, test-controlled values
   * (overrides whatever the environment produced in the constructor).
   */
  Configurator::DOSDetectorConfig makeConfig(
      const uint32_t maxRequests = 10,
      const uint32_t maxFingerprints =
          10,  // Thresholds: repeats of one fingerprint, then number of
               // distinct fingerprints.
      const std::chrono::seconds period = std::chrono::seconds{60},
      const std::chrono::seconds rlDuration =
          std::chrono::seconds{30},  // Sliding window length, then how long a
                                     // rate limit lasts.
      const std::chrono::seconds banDuration = std::chrono::seconds{3600},
      const std::unordered_set<std::string> whitelist =
          {},  // Ban length, then IPs allowed regardless of traffic.
      const std::unordered_set<std::string> blacklist =
          {})  // IPs blocked outright; defaults keep existing tests unchanged
               // when omitted.
  {
    Configurator::DOSDetectorConfig config;  // A fresh settings object to fill
                                             // with the explicit values above.
    config.max_requests =
        maxRequests;  // How many identical requests trigger a ban.
    config.max_fingerprints =
        maxFingerprints;  // How many different fingerprints trigger a rate
                          // limit.
    config.period =
        period;  // Length of the sliding window those counts are kept in.
    config.ratelimit_duration = rlDuration;  // How long a rate limit stays in
                                             // force after it is applied.
    config.ban_duration =
        banDuration;  // How long a ban stays in force after it is applied.
    // Long cleanup cadence: the background cleaner must not fire (on the
    // real clock) while a test manipulates the fake clock.
    config.clean_freq =
        3600;  // One hour on the real clock, so the cleaner never runs
               // mid-test and disturbs the fake clock.
    config.whitelist = whitelist;  // Copy the caller's always-allowed list
                                   // into the config.
    config.blacklist = blacklist;  // Copy the caller's always-blocked list
                                   // into the config.
    return config;  // Hand the fully specified config back to the test.
  }

  /** @brief Builds a detector request for a test.
   *
   * Method and path are fixed; only the fields that influence a fingerprint
   * (ip, body and the hashed headers) vary between cases.
   */
  DOSDetector::Request makeRequest(
      const std::string&                                 ip,
      const std::string&                                 body,
      const std::unordered_map<std::string, std::string> headers =
          {})  // Builds a request with a fixed method/path and
               // caller-supplied IP, body and headers.
  {
    return DOSDetector::Request{.ip     = ip,
                                .method = "GET",
                                .path   = "/users/read",
                                .header_hash =
                                    DOSDetector::hashHeaders(headers),
                                .body = body};  // Designated initializers make
                                                // it obvious which fingerprint
                                                // field is which; headers are
                                                // hashed into one number.
  }

  /**
   * Detector + fake clock pair. The clock starts "now" and is advanced by
   * tests to simulate time passing without any real waiting.
   */
  struct DetectorFixture
  {
    FakeClock clock;  // The controllable clock injected into the detector;
                      // advancing it simulates time passing instantly.
    std::unique_ptr<DOSDetector> detector;  // The component under test, owned
                                            // uniquely by this fixture.
    std::vector<std::string> bodies;  // keeps string_views alive -- requests
                                      // store views into these strings.

    explicit DetectorFixture(Configurator::DOSDetectorConfig config)
        : detector(std::make_unique<DOSDetector>(std::move(config),
                                                 clock.callable()))
    {
    }  // Build the detector with the fake clock's callable; 'explicit'
       // prevents an accidental implicit conversion from config.

    DOSDetector::Status send(
        const std::string& ip,
        const std::string& body)  // Sends a request with no extra headers
                                  // and returns the detector's verdict.
    {
      bodies.push_back(body);  // body is captured as string_view -- keep
                               // the string alive in the vector.
      return detector->is_dos_attack(
          makeRequest(ip,
                      bodies.back()));  // Pass the request to the detector and
                                        // return ALLOWED / RATELIMITED / BANNED
                                        // / etc.
    }

    DOSDetector::Status send(
        const std::string& ip,
        const std::string& body,
        const std::unordered_map<std::string, std::string>&
            headers)  // Overload that also varies the headers, which change
                      // the fingerprint.
    {
      bodies.push_back(body);  // body is captured as string_view -- keep
                               // the string alive in the vector.
      return detector->is_dos_attack(
          makeRequest(ip,
                      bodies.back(),
                      headers));  // Same as above but with a header hash in the
                                  // fingerprint.
    }
  };

  // RFC 5737 TEST-NET-3 address: a safe dummy public IP for the tests that
  // key the detector's state by client address.
  const std::string IP = "203.0.113.10";  // A documentation-only IP range, so
                                          // tests never target a real machine.
}  // namespace

TEST_CASE("DoSDetector anchored whitelist patterns do not over-match",
          "[gatekeeper][dos]")  // Registers the test that a whitelist entry
                                // must match exactly, not as a broad prefix.
{
  DetectorFixture fixture(
      makeConfig(/*maxRequests*/ 10,
                 /*maxFingerprints*/ 100,
                 std::chrono::seconds{60},
                 std::chrono::seconds{30},
                 std::chrono::seconds{3600}, /*whitelist*/
                 {"172.17.0.1"}));  // Configure one whitelisted IP with
                                    // generous thresholds so nothing else can
                                    // trigger a ban.

  REQUIRE(fixture.send("172.17.0.1", "body") ==
          DOSDetector::Status::WHITELISTED);  // Prove the exact IP in the
                                              // whitelist is recognised.
  // An anchored pattern must not also match 172.17.0.10 .. 172.17.0.199.
  REQUIRE(fixture.send("172.17.0.10", "body") ==
          DOSDetector::Status::ALLOWED);  // Prove a longer IP starting with
                                          // the same text is treated
                                          // normally, not whitelisted.
}

TEST_CASE("DoSDetector allows requests under the repeat limit",
          "[gatekeeper][dos]")  // Registers the test for the normal,
                                // below-threshold case.
{
  DetectorFixture fixture(
      makeConfig(/*maxRequests*/ 5, /*maxFingerprints*/
                 100));  // A ban needs more than 5 repeats; the other threshold
                         // is high enough not to interfere.

  for (int i = 1; i <= 5; ++i)  // Send exactly the allowed number of repeats;
                                // the loop stops after the 5th.
  {
    CAPTURE(i);  // If an assertion fails, Catch2 prints the current i so we
                 // know which request failed.
    REQUIRE(fixture.send(IP, "same-body") ==
            DOSDetector::Status::ALLOWED);  // Every request at or below the
                                            // limit must still be allowed.
  }
}

TEST_CASE(
    "DoSDetector bans an IP when one request fingerprint repeats more than "
    "MAX_REQUESTS times",
    "[gatekeeper][dos]")  // Registers the test for the repeat-count ban.
{
  DetectorFixture fixture(
      makeConfig(/*maxRequests*/ 5,
                 /*maxFingerprints*/ 100,
                 std::chrono::seconds{60},
                 std::chrono::seconds{30},
                 std::chrono::seconds{30}));  // Ban lasts 30 s; window is 60 s.

  for (int i = 0; i < 5; ++i)  // Send the five requests that are still allowed;
                               // stop before the threshold is crossed.
  {
    REQUIRE(fixture.send(IP, "same-body") ==
            DOSDetector::Status::ALLOWED);  // Each of the first five identical
                                            // requests stays allowed.
  }
  // 6th identical request within the period crosses MAX_REQUESTS.
  REQUIRE(fixture.send(IP, "same-body") ==
          DOSDetector::Status::BANNED);  // Prove one more repeat flips the
                                         // verdict to BANNED.

  // While banned, further requests stay banned (no time passed).
  REQUIRE(fixture.send(IP, "other-body") ==
          DOSDetector::Status::BANNED);  // Prove even a different body is
                                         // blocked while the ban is active.
}

TEST_CASE("DoSDetector ban expires after BAN_DURATION",
          "[gatekeeper][dos]")  // Registers the test for ban expiry, using two
                                // sections for different elapsed times.
{
  DetectorFixture fixture(
      makeConfig(/*maxRequests*/ 2,
                 /*maxFingerprints*/ 100,
                 std::chrono::seconds{60},
                 std::chrono::seconds{30},
                 /*banDuration*/
                 std::chrono::seconds{30}));  // A short 30 s ban makes the two
                                              // time choices meaningful.

  // Trigger a ban (3 identical requests > MAX_REQUESTS=2).
  (void)fixture.send(IP,
                     "body");  // First request; the cast discards the status
                               // because only the final one matters here.
  (void)fixture
      .send(IP, "body");  // Second request, still building up the repeat count.
  REQUIRE(fixture.send(IP, "body") ==
          DOSDetector::Status::BANNED);  // Third identical request crosses
                                         // the limit and bans the IP.

  SECTION("still banned before the ban duration elapses")
  {
    fixture.clock.advance(
        std::chrono::seconds{29});  // Move the fake clock 29 s forward,
                                    // just short of the 30 s ban.
    REQUIRE(fixture.send(IP, "body") ==
            DOSDetector::Status::BANNED);  // Prove the ban is still in
                                           // force one second early.
  }

  SECTION(
      "allowed again once the ban duration has elapsed (window also rolled "
      "over)")
  {
    fixture.clock.advance(std::chrono::seconds{
        61});  // > BAN_DURATION and > PERIOD -- jump past both limits.
    REQUIRE(fixture.send(IP, "body") ==
            DOSDetector::Status::ALLOWED);  // Prove the IP is fully
                                            // rehabilitated once both
                                            // timers expire.
  }
}

TEST_CASE(
    "DoSDetector rate-limits an IP whose distinct request fingerprints exceed "
    "MAX_FPS",
    "[gatekeeper][dos]")  // Registers the test for the distinct-fingerprint
                          // (rate-limit) rule.
{
  DetectorFixture fixture(
      makeConfig(/*maxRequests*/ 100,
                 /*maxFingerprints*/ 2,
                 std::chrono::seconds{60},
                 std::chrono::seconds{30},
                 std::chrono::seconds{3600}));  // A high repeat limit isolates
                                                // the fingerprint rule.

  REQUIRE(fixture.send(IP, "body-a") ==
          DOSDetector::Status::ALLOWED);  // First distinct fingerprint is fine.
  REQUIRE(fixture.send(IP, "body-b") ==
          DOSDetector::Status::ALLOWED);  // Second distinct fingerprint is
                                          // still fine.
  // Third distinct fingerprint crosses MAX_FPS.
  REQUIRE(
      fixture.send(IP, "body-c") ==
      DOSDetector::Status::RATELIMITED);  // Prove a third different body
                                          // triggers a rate limit, not a ban.

  // Still rate-limited without time passing.
  REQUIRE(fixture.send(IP, "body-c") ==
          DOSDetector::Status::RATELIMITED);  // Prove the limit persists while
                                              // the timer has not advanced.
}

TEST_CASE(
    "DoSDetector treats different bodies and headers as different fingerprints",
    "[gatekeeper][dos]")  // Registers the test showing which request fields
                          // form a fingerprint.
{
  DetectorFixture fixture(
      makeConfig(/*maxRequests*/ 100, /*maxFingerprints*/
                 4));  // Four distinct fingerprints allowed, so none of these
                       // four should trip the limit.

  REQUIRE(fixture.send(IP, "body-a") ==
          DOSDetector::Status::ALLOWED);  // Body a contributes one fingerprint.
  REQUIRE(fixture.send(IP, "body-b") ==
          DOSDetector::Status::ALLOWED);  // Body b is a different fingerprint.
  // Same body, but a different header value changes the fingerprint.
  REQUIRE(fixture.send(IP, "body-a", {{"x-tag", "one"}}) ==
          DOSDetector::Status::ALLOWED);  // Same body but a header makes a
                                          // third fingerprint.
  REQUIRE(fixture.send(IP, "body-a", {{"x-tag", "two"}}) ==
          DOSDetector::Status::ALLOWED);  // A different header value makes a
                                          // fourth, proving headers are
                                          // hashed into the fingerprint.
}

TEST_CASE(
    "DoSDetector rate limit expires after RL_DURATION and then re-arms while "
    "the window is saturated",
    "[gatekeeper][dos]")  // Registers the test for the subtle interaction
                          // between the rate-limit timer and the sliding
                          // window.
{
  DetectorFixture fixture(
      makeConfig(/*maxRequests*/ 100,
                 /*maxFingerprints*/ 2,
                 std::chrono::seconds{60},
                 /*rlDuration*/ std::chrono::seconds{10},
                 std::chrono::seconds{
                     3600}));  // 2 fingerprints allowed, rate limit
                               // lasts 10 s, window is 60 s.

  (void)fixture
      .send(IP, "a");  // First distinct fingerprint; result unimportant here.
  (void)fixture.send(IP,
                     "b");  // Second distinct fingerprint fills the allowance.
  REQUIRE(
      fixture.send(IP, "c") ==
      DOSDetector::Status::RATELIMITED);  // A third distinct body crosses
                                          // MAX_FPS and starts a rate limit.

  fixture.clock.advance(std::chrono::seconds{
      9});  // Move to 9 s, one second before the rate limit should expire.
  REQUIRE(fixture.send(IP, "c") ==
          DOSDetector::Status::RATELIMITED);  // Prove the limit still applies
                                              // just before expiry.

  // Rate limit duration elapsed (10s), but the fingerprint window is still
  // saturated (3 distinct fingerprints > MAX_FPS), so the IP is immediately
  // rate-limited again. This documents the current interplay between
  // RL_DURATION and PERIOD.
  fixture.clock.advance(
      std::chrono::seconds{1});  // Cross the 10 s rate-limit boundary; the 60
                                 // s window has not rolled over.
  REQUIRE(fixture.send(IP, "c") ==
          DOSDetector::Status::RATELIMITED);  // Prove the stale-window
                                              // fingerprints immediately
                                              // re-arm the limit.
}

TEST_CASE(
    "DoSDetector window rollover: old requests no longer count against the "
    "repeat limit",
    "[gatekeeper][dos]")  // Registers the test showing windows and bans expire
                          // independently.
{
  SECTION("while the ban is still active, the IP stays banned")
  {
    DetectorFixture fixture(
        makeConfig(/*maxRequests*/ 3,
                   /*maxFingerprints*/ 100,
                   /*period*/ std::chrono::seconds{30},
                   std::chrono::seconds{30},
                   /*banDuration*/
                   std::chrono::seconds{
                       3600}));  // A 30 s window but a long 1-hour ban.

    for (int i = 0; i < 3;
         ++i)  // Build up exactly the allowed number of repeats.
    {
      REQUIRE(fixture.send(IP, "same-body") ==
              DOSDetector::Status::ALLOWED);  // Each of the first three
                                              // requests is allowed.
    }
    REQUIRE(fixture.send(IP, "same-body") ==
            DOSDetector::Status::BANNED);  // The fourth repeat triggers the
                                           // ban.

    // 31s later the fingerprint window (period 30s) has rolled over, but
    // the ban (3600s) is still active, so the IP remains banned.
    fixture.clock.advance(std::chrono::seconds{
        31});  // Move past the window length but stay well inside the ban.
    REQUIRE(fixture.send(IP, "same-body") ==
            DOSDetector::Status::BANNED);  // Prove an expired window does
                                           // NOT cancel an active ban.
  }

  SECTION("after both ban and window expired the request is allowed again")
  {
    DetectorFixture fixture(
        makeConfig(/*maxRequests*/ 3,
                   /*maxFingerprints*/ 100,
                   /*period*/ std::chrono::seconds{30},
                   std::chrono::seconds{30},
                   /*banDuration*/
                   std::chrono::seconds{
                       30}));  // Here both the window and the ban last 30 s.

    for (int i = 0; i < 3;
         ++i)  // Build up the repeat count without checking each step.
    {
      (void)fixture.send(IP,
                         "same-body");  // Send a repeat; the ban check below is
                                        // the one that matters.
    }
    REQUIRE(fixture.send(IP, "same-body") ==
            DOSDetector::Status::BANNED);  // Fourth repeat bans the IP.

    // Past BAN_DURATION the ban lifted, and the fingerprint window
    // (PERIOD 30s) has rolled over, so the repeat count starts fresh.
    fixture.clock.advance(std::chrono::seconds{
        31});  // Move past both the 30 s ban and the 30 s window.
    REQUIRE(fixture.send(IP, "same-body") ==
            DOSDetector::Status::ALLOWED);  // Prove the IP is allowed again
                                            // once both have expired.
  }
}

TEST_CASE("DoSDetector whitelists IPs by exact match and regex",
          "[gatekeeper][dos]")  // Registers the test for the two whitelist
                                // forms: literal and regular-expression.
{
  auto config = makeConfig();  // Start from default thresholds.
  config
      .whitelist = {"192.168.1.5",
                    "re:^10\\.0\\..*"};  // One exact IP and one regex (the
                                         // "re:" prefix marks a regex pattern).
  DetectorFixture fixture(std::move(
      config));  // Build the detector; moving avoids copying the config.

  CHECK(fixture.send("192.168.1.5", "x") ==
        DOSDetector::Status::WHITELISTED);  // Prove an exact whitelist entry
                                            // is honoured.

  SECTION("regex whitelist match")
  {
    CHECK(fixture.send("10.0.42.7", "x") == DOSDetector::Status::WHITELISTED);
  }  // Prove the regex matches an IP in the 10.0.* range.

  SECTION("a non-matching IP is processed normally")
  {
    CHECK(fixture.send("192.168.1.6", "x") == DOSDetector::Status::ALLOWED);
  }  // Prove an IP that matches neither pattern is not whitelisted.
}

TEST_CASE("DoSDetector blacklists IPs before processing",
          "[gatekeeper][dos]")  // Registers the test for outright blocking.
{
  auto config      = makeConfig();      // Start from default thresholds.
  config.blacklist = {"203.0.113.99"};  // Put one dummy IP on the blacklist.
  DetectorFixture fixture(
      std::move(config));  // Build the detector with that blacklist.

  REQUIRE(fixture.send("203.0.113.99", "x") ==
          DOSDetector::Status::BLACKLISTED);  // Prove a blacklisted IP is
                                              // rejected immediately.
  // Repeatedly - no state accumulation needed.
  REQUIRE(fixture.send("203.0.113.99", "x") ==
          DOSDetector::Status::BLACKLISTED);  // Prove the block is stateless:
                                              // repeated calls stay blocked
                                              // without building up counters.
}

TEST_CASE("DoSDetector whitelist wins over blacklist",
          "[gatekeeper][dos]")  // Registers the test for precedence when an IP
                                // is on both lists.
{
  auto config      = makeConfig();      // Start from default thresholds.
  config.whitelist = {"203.0.113.50"};  // Add the IP to the allow list...
  config.blacklist = {
      "203.0.113.50"};  // ...and also to the deny list, creating a conflict.
  DetectorFixture fixture(std::move(
      config));  // Build the detector with the conflicting configuration.

  REQUIRE(fixture.send("203.0.113.50", "x") ==
          DOSDetector::Status::WHITELISTED);  // Prove the whitelist takes
                                              // priority over the blacklist.
}

TEST_CASE(
    "DoSDetector ignores invalid whitelist regex patterns instead of crashing",
    "[gatekeeper][dos]")  // Registers the robustness test for bad
                          // configuration.
{
  auto config = makeConfig();  // Start from default thresholds.
  config.whitelist =
      {"[invalid-regex",
       "203.0.113.70"};  // One malformed regex and one valid literal entry.
  DetectorFixture fixture(std::move(
      config));  // Building the detector must survive the bad pattern.

  CHECK(fixture.send("203.0.113.70", "x") ==
        DOSDetector::Status::WHITELISTED);  // Prove the valid entry still
                                            // works despite the bad one.
  CHECK(fixture.send("203.0.113.71", "x") ==
        DOSDetector::Status::ALLOWED);  // Prove the malformed pattern matches
                                        // nothing and does not crash.
}

TEST_CASE("DoSDetector handles concurrent callers without deadlock",
          "[gatekeeper][dos][concurrency]")  // Registers the test that the
                                             // detector is safe when many
                                             // threads call it at once.
{
  DetectorFixture fixture(
      makeConfig(/*maxRequests*/ 100000,
                 /*maxFingerprints*/
                 100000));  // Very high limits so nothing is ever blocked; we
                            // are probing for races and deadlocks, not policy.

  constexpr int kThreads = 4;  // Number of simultaneous callers.
  constexpr int kRequests =
      25;  // Requests each caller sends, giving a total of 100.

  // Pre-populate all request bodies before spawning threads;
  // DetectorFixture::send calls bodies.push_back() which is not thread-safe
  // under concurrent access.
  std::vector<std::vector<std::string>> threadBodies(
      kThreads);  // One private list of strings per thread, so no shared
                  // writes while threads run.
  for (int t = 0; t < kThreads;
       ++t)  // Loop over threads, building each thread's strings in advance.
    for (int i = 0; i < kRequests; ++i)
      threadBodies[t].push_back(
          "body-" + std::to_string(t) + "-" +
          std::to_string(i));  // Give every request a unique body so
                               // fingerprints stay distinct.

  std::vector<std::thread>
      workers;  // Holds the worker threads so we can join them afterwards.
  std::atomic<int> allowedCount{
      0};  // Atomic tally of requests that came back ALLOWED.
  std::atomic<bool> unexpected{false};  // Atomic flag set if any request gets
                                        // a status we did not expect.

  for (int t = 0; t < kThreads;
       ++t)  // Start one worker thread per entry in threadBodies.
  {
    workers.emplace_back(
        [&fixture, &allowedCount, &unexpected, &threadBodies, t]()
        {
          for (int i = 0; i < kRequests;
               ++i)  // Each worker sends its own pre-built requests.
          {
            const auto req =
                makeRequest("203.0.113.77",
                            threadBodies[t][i]);  // Build a request from this
                                                  // thread's own string.
            const auto status = fixture.detector->is_dos_attack(
                req);  // Call the detector concurrently; this must not
                       // crash or deadlock.
            if (status ==
                DOSDetector::Status::ALLOWED)  // Expected outcome: the
                                               // high thresholds should
                                               // allow everything.
            {
              ++allowedCount;  // Count a successful request.
            }
            else
            {
              unexpected = true;  // Flag any other status so the
                                  // final assertion fails.
            }
          }
        });
  }
  for (auto& worker : workers)  // Iterate over all workers.
  {
    worker.join();  // Wait for each to finish; without this, the test could
                    // read counters while threads still write them.
  }

  REQUIRE_FALSE(
      unexpected.load());  // Prove no caller saw an unexpected verdict (e.g.
                           // a spurious ban from a race).
  REQUIRE(allowedCount.load() ==
          kThreads * kRequests);  // Prove every request was processed and
                                  // allowed: 4 × 25 = 100.
}

TEST_CASE("DoSDetector keeps working after the configured cleanup never fires",
          "[gatekeeper][dos]")  // Registers the test that lazy expiry works
                                // without the background cleaner.
{
  // clean_freq is 3600s (real time) in these fixtures: the background
  // cleaner does not run during a test, which must not affect correctness
  // of the per-request paths (they trim windows and expire blocks lazily).
  DetectorFixture fixture(
      makeConfig(/*maxRequests*/ 2,
                 /*maxFingerprints*/ 2,
                 std::chrono::seconds{60},
                 std::chrono::seconds{30},
                 std::chrono::seconds{3600}));  // 2 fingerprints allowed, 60 s
                                                // window, 30 s rate limit.

  (void)fixture.send(IP, "a");  // First distinct fingerprint.
  (void)fixture.send(IP,
                     "b");  // Second distinct fingerprint fills the allowance.
  REQUIRE(fixture.send(IP, "c") ==
          DOSDetector::Status::RATELIMITED);  // Third distinct body triggers
                                              // the rate limit.
  fixture.clock.advance(std::chrono::seconds{
      31});  // beyond period and RL duration -- jump past both timers.
  // Window is saturated with 2 distinct fingerprints == MAX_FPS; a new
  // fingerprint re-arms the rate limit.
  REQUIRE(fixture.send(IP, "c") ==
          DOSDetector::Status::RATELIMITED);  // Prove the stale window still
                                              // re-limits even though the
                                              // cleaner never ran.
}

// Bug 7 fixed (docs): README.md now correctly describes:
//   MAX_REQUESTS — repeat-count threshold before BAN
//   MAX_FPS      — distinct-fingerprint threshold before RATE-LIMIT
TEST_CASE(
    "DoSDetector: MAX_REQUESTS triggers a ban; MAX_FPS triggers a rate-limit",
    "[gatekeeper][dos]")  // Registers the test that documents which rule fires
                          // for repeated identical requests.
{
  // "MAX_REQUESTS=2 bans after 3 identical requests" — rate-limit would be
  // the documented expectation; ban is the actual result.
  DetectorFixture fixture(
      makeConfig(/*maxRequests*/ 2,
                 /*maxFingerprints*/ 100,
                 std::chrono::seconds{60},
                 std::chrono::seconds{30},
                 std::chrono::seconds{
                     3600}));  // High fingerprint limit isolates the
                               // repeat-count rule.

  REQUIRE(fixture.send(IP, "same-body") ==
          DOSDetector::Status::ALLOWED);  // First identical request is allowed.
  REQUIRE(fixture.send(IP, "same-body") ==
          DOSDetector::Status::ALLOWED);  // Second identical request is still
                                          // allowed.
  // Third identical request → BANNED (not RATELIMITED as the README implies).
  CHECK(fixture.send(IP, "same-body") ==
        DOSDetector::Status::BANNED);  // Prove the actual behaviour is a ban,
                                       // pinning down the documented
                                       // distinction.
}
