#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <chrono>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "configurator/configurator.hpp"
#include "support/builders.hpp"
#include "support/fake_clock.hpp"

using DOSDetector = ::DOSDetector;

namespace
{
    /// Builds a DOSDetectorConfig with explicit, test-controlled values
    /// (overrides whatever the environment produced in the constructor).
    Configurator::DOSDetectorConfig makeConfig(const uint32_t maxRequests = 10, const uint32_t maxFingerprints = 10,
        const std::chrono::seconds period = std::chrono::seconds{60}, const std::chrono::seconds rlDuration = std::chrono::seconds{30},
        const std::chrono::seconds banDuration = std::chrono::seconds{3600}, const std::unordered_set<std::string> whitelist = {},
        const std::unordered_set<std::string> blacklist = {})
    {
        Configurator::DOSDetectorConfig config;
        config.max_requests       = maxRequests;
        config.max_fingerprints   = maxFingerprints;
        config.period             = period;
        config.ratelimit_duration = rlDuration;
        config.ban_duration       = banDuration;
        // Long cleanup cadence: the background cleaner must not fire (on the
        // real clock) while a test manipulates the fake clock.
        config.clean_freq = 3600;
        config.whitelist  = whitelist;
        config.blacklist  = blacklist;
        return config;
    }

    DOSDetector::Request makeRequest(const std::string& ip, const std::string& body, const std::unordered_map<std::string, std::string> headers = {})
    {
        return DOSDetector::Request{.ip = ip, .method = "GET", .path = "/users/read", .headers = headers, .body = body};
    }

    /// Detector + fake clock pair. The clock starts "now" and is advanced by
    /// tests to simulate time passing without any real waiting.
    struct DetectorFixture
    {
        FakeClock                    clock;
        std::unique_ptr<DOSDetector> detector;
        std::vector<std::string>     bodies;  // keeps string_views alive

        explicit DetectorFixture(Configurator::DOSDetectorConfig config) : detector(std::make_unique<DOSDetector>(std::move(config), clock.callable())) {}

        DOSDetector::Status send(const std::string& ip, const std::string& body)
        {
            bodies.push_back(body);  // body is captured as string_view
            return detector->is_dos_attack(makeRequest(ip, bodies.back()));
        }

        DOSDetector::Status send(const std::string& ip, const std::string& body, const std::unordered_map<std::string, std::string>& headers)
        {
            bodies.push_back(body);  // body is captured as string_view
            return detector->is_dos_attack(makeRequest(ip, bodies.back(), headers));
        }
    };

    const std::string IP = "203.0.113.10";
}  // namespace

TEST_CASE("DoSDetector allows requests under the repeat limit", "[gatekeeper][dos]")
{
    DetectorFixture fixture(makeConfig(/*maxRequests*/ 5, /*maxFingerprints*/ 100));

    for (int i = 1; i <= 5; ++i)
    {
        CAPTURE(i);
        REQUIRE(fixture.send(IP, "same-body") == DOSDetector::Status::ALLOWED);
    }
}

TEST_CASE("DoSDetector bans an IP when one request fingerprint repeats more than MAX_REQUESTS times", "[gatekeeper][dos]")
{
    DetectorFixture fixture(
        makeConfig(/*maxRequests*/ 5, /*maxFingerprints*/ 100, std::chrono::seconds{60}, std::chrono::seconds{30}, std::chrono::seconds{30}));

    for (int i = 0; i < 5; ++i)
    {
        REQUIRE(fixture.send(IP, "same-body") == DOSDetector::Status::ALLOWED);
    }
    // 6th identical request within the period crosses MAX_REQUESTS.
    REQUIRE(fixture.send(IP, "same-body") == DOSDetector::Status::BANNED);

    // While banned, further requests stay banned (no time passed).
    REQUIRE(fixture.send(IP, "other-body") == DOSDetector::Status::BANNED);
}

TEST_CASE("DoSDetector ban expires after BAN_DURATION", "[gatekeeper][dos]")
{
    DetectorFixture fixture(makeConfig(/*maxRequests*/ 2, /*maxFingerprints*/ 100, std::chrono::seconds{60}, std::chrono::seconds{30},
        /*banDuration*/ std::chrono::seconds{30}));

    // Trigger a ban (3 identical requests > MAX_REQUESTS=2).
    (void)fixture.send(IP, "body");
    (void)fixture.send(IP, "body");
    REQUIRE(fixture.send(IP, "body") == DOSDetector::Status::BANNED);

    SECTION("still banned before the ban duration elapses")
    {
        fixture.clock.advance(std::chrono::seconds{29});
        REQUIRE(fixture.send(IP, "body") == DOSDetector::Status::BANNED);
    }

    SECTION("allowed again once the ban duration has elapsed (window also rolled over)")
    {
        fixture.clock.advance(std::chrono::seconds{61});  // > BAN_DURATION and > PERIOD
        REQUIRE(fixture.send(IP, "body") == DOSDetector::Status::ALLOWED);
    }
}

TEST_CASE("DoSDetector rate-limits an IP whose distinct request fingerprints exceed MAX_FPS", "[gatekeeper][dos]")
{
    DetectorFixture fixture(
        makeConfig(/*maxRequests*/ 100, /*maxFingerprints*/ 2, std::chrono::seconds{60}, std::chrono::seconds{30}, std::chrono::seconds{3600}));

    REQUIRE(fixture.send(IP, "body-a") == DOSDetector::Status::ALLOWED);
    REQUIRE(fixture.send(IP, "body-b") == DOSDetector::Status::ALLOWED);
    // Third distinct fingerprint crosses MAX_FPS.
    REQUIRE(fixture.send(IP, "body-c") == DOSDetector::Status::RATELIMITED);

    // Still rate-limited without time passing.
    REQUIRE(fixture.send(IP, "body-c") == DOSDetector::Status::RATELIMITED);
}

TEST_CASE("DoSDetector treats different bodies and headers as different fingerprints", "[gatekeeper][dos]")
{
    DetectorFixture fixture(makeConfig(/*maxRequests*/ 100, /*maxFingerprints*/ 4));

    REQUIRE(fixture.send(IP, "body-a") == DOSDetector::Status::ALLOWED);
    REQUIRE(fixture.send(IP, "body-b") == DOSDetector::Status::ALLOWED);
    // Same body, but a different header value changes the fingerprint.
    REQUIRE(fixture.send(IP, "body-a", {{"x-tag", "one"}}) == DOSDetector::Status::ALLOWED);
    REQUIRE(fixture.send(IP, "body-a", {{"x-tag", "two"}}) == DOSDetector::Status::ALLOWED);
}

TEST_CASE("DoSDetector rate limit expires after RL_DURATION and then re-arms while the window is saturated", "[gatekeeper][dos]")
{
    DetectorFixture fixture(
        makeConfig(/*maxRequests*/ 100, /*maxFingerprints*/ 2, std::chrono::seconds{60}, /*rlDuration*/ std::chrono::seconds{10}, std::chrono::seconds{3600}));

    (void)fixture.send(IP, "a");
    (void)fixture.send(IP, "b");
    REQUIRE(fixture.send(IP, "c") == DOSDetector::Status::RATELIMITED);

    fixture.clock.advance(std::chrono::seconds{9});
    REQUIRE(fixture.send(IP, "c") == DOSDetector::Status::RATELIMITED);

    // Rate limit duration elapsed (10s), but the fingerprint window is still
    // saturated (3 distinct fingerprints > MAX_FPS), so the IP is immediately
    // rate-limited again. This documents the current interplay between
    // RL_DURATION and PERIOD.
    fixture.clock.advance(std::chrono::seconds{1});
    REQUIRE(fixture.send(IP, "c") == DOSDetector::Status::RATELIMITED);
}

TEST_CASE("DoSDetector window rollover: old requests no longer count against the repeat limit", "[gatekeeper][dos]")
{
    SECTION("while the ban is still active, the IP stays banned")
    {
        DetectorFixture fixture(makeConfig(/*maxRequests*/ 3, /*maxFingerprints*/ 100, /*period*/ std::chrono::seconds{30}, std::chrono::seconds{30},
            /*banDuration*/ std::chrono::seconds{3600}));

        for (int i = 0; i < 3; ++i)
        {
            REQUIRE(fixture.send(IP, "same-body") == DOSDetector::Status::ALLOWED);
        }
        REQUIRE(fixture.send(IP, "same-body") == DOSDetector::Status::BANNED);

        // 31s later the fingerprint window (period 30s) has rolled over, but
        // the ban (3600s) is still active, so the IP remains banned.
        fixture.clock.advance(std::chrono::seconds{31});
        REQUIRE(fixture.send(IP, "same-body") == DOSDetector::Status::BANNED);
    }

    SECTION("after both ban and window expired the request is allowed again")
    {
        DetectorFixture fixture(makeConfig(/*maxRequests*/ 3, /*maxFingerprints*/ 100, /*period*/ std::chrono::seconds{30}, std::chrono::seconds{30},
            /*banDuration*/ std::chrono::seconds{30}));

        for (int i = 0; i < 3; ++i)
        {
            (void)fixture.send(IP, "same-body");
        }
        REQUIRE(fixture.send(IP, "same-body") == DOSDetector::Status::BANNED);

        // Past BAN_DURATION the ban lifted, and the fingerprint window
        // (PERIOD 30s) has rolled over, so the repeat count starts fresh.
        fixture.clock.advance(std::chrono::seconds{31});
        REQUIRE(fixture.send(IP, "same-body") == DOSDetector::Status::ALLOWED);
    }
}

TEST_CASE("DoSDetector whitelists IPs by exact match and regex", "[gatekeeper][dos]")
{
    auto config      = makeConfig();
    config.whitelist = {"192.168.1.5", R"(^10\.0\..*)"};
    DetectorFixture fixture(std::move(config));

    CHECK(fixture.send("192.168.1.5", "x") == DOSDetector::Status::WHITELISTED);

    SECTION("regex whitelist match") { CHECK(fixture.send("10.0.42.7", "x") == DOSDetector::Status::WHITELISTED); }

    SECTION("a non-matching IP is processed normally") { CHECK(fixture.send("192.168.1.6", "x") == DOSDetector::Status::ALLOWED); }
}

TEST_CASE("DoSDetector blacklists IPs before processing", "[gatekeeper][dos]")
{
    auto config      = makeConfig();
    config.blacklist = {"203.0.113.99"};
    DetectorFixture fixture(std::move(config));

    REQUIRE(fixture.send("203.0.113.99", "x") == DOSDetector::Status::BLACKLISTED);
    // Repeatedly - no state accumulation needed.
    REQUIRE(fixture.send("203.0.113.99", "x") == DOSDetector::Status::BLACKLISTED);
}

TEST_CASE("DoSDetector whitelist wins over blacklist", "[gatekeeper][dos]")
{
    auto config      = makeConfig();
    config.whitelist = {"203.0.113.50"};
    config.blacklist = {"203.0.113.50"};
    DetectorFixture fixture(std::move(config));

    REQUIRE(fixture.send("203.0.113.50", "x") == DOSDetector::Status::WHITELISTED);
}

TEST_CASE("DoSDetector ignores invalid whitelist regex patterns instead of crashing", "[gatekeeper][dos]")
{
    auto config      = makeConfig();
    config.whitelist = {"[invalid-regex", "203.0.113.70"};
    DetectorFixture fixture(std::move(config));

    CHECK(fixture.send("203.0.113.70", "x") == DOSDetector::Status::WHITELISTED);
    CHECK(fixture.send("203.0.113.71", "x") == DOSDetector::Status::ALLOWED);
}

TEST_CASE("DoSDetector handles concurrent callers without deadlock", "[gatekeeper][dos][concurrency]")
{
    DetectorFixture fixture(makeConfig(/*maxRequests*/ 100000, /*maxFingerprints*/ 100000));

    constexpr int kThreads  = 4;
    constexpr int kRequests = 25;

    // Pre-populate all request bodies before spawning threads; DetectorFixture::send
    // calls bodies.push_back() which is not thread-safe under concurrent access.
    std::vector<std::vector<std::string>> threadBodies(kThreads);
    for (int t = 0; t < kThreads; ++t)
        for (int i = 0; i < kRequests; ++i) threadBodies[t].push_back("body-" + std::to_string(t) + "-" + std::to_string(i));

    std::vector<std::thread> workers;
    std::atomic<int>         allowedCount{0};
    std::atomic<bool>        unexpected{false};

    for (int t = 0; t < kThreads; ++t)
    {
        workers.emplace_back(
            [&fixture, &allowedCount, &unexpected, &threadBodies, t]()
            {
                for (int i = 0; i < kRequests; ++i)
                {
                    const auto req    = makeRequest("203.0.113.77", threadBodies[t][i]);
                    const auto status = fixture.detector->is_dos_attack(req);
                    if (status == DOSDetector::Status::ALLOWED)
                    {
                        ++allowedCount;
                    }
                    else
                    {
                        unexpected = true;
                    }
                }
            });
    }
    for (auto& worker : workers)
    {
        worker.join();
    }

    REQUIRE_FALSE(unexpected.load());
    REQUIRE(allowedCount.load() == kThreads * kRequests);
}

TEST_CASE("DoSDetector keeps working after the configured cleanup never fires", "[gatekeeper][dos]")
{
    // clean_freq is 3600s (real time) in these fixtures: the background
    // cleaner does not run during a test, which must not affect correctness
    // of the per-request paths (they trim windows and expire blocks lazily).
    DetectorFixture fixture(
        makeConfig(/*maxRequests*/ 2, /*maxFingerprints*/ 2, std::chrono::seconds{60}, std::chrono::seconds{30}, std::chrono::seconds{3600}));

    (void)fixture.send(IP, "a");
    (void)fixture.send(IP, "b");
    REQUIRE(fixture.send(IP, "c") == DOSDetector::Status::RATELIMITED);
    fixture.clock.advance(std::chrono::seconds{31});  // beyond period and RL duration
    // Window is saturated with 2 distinct fingerprints == MAX_FPS; a new
    // fingerprint re-arms the rate limit.
    REQUIRE(fixture.send(IP, "c") == DOSDetector::Status::RATELIMITED);
}

// Bug 7 fixed (docs): README.md now correctly describes:
//   MAX_REQUESTS — repeat-count threshold before BAN
//   MAX_FPS      — distinct-fingerprint threshold before RATE-LIMIT
TEST_CASE("DoSDetector: MAX_REQUESTS triggers a ban; MAX_FPS triggers a rate-limit", "[gatekeeper][dos]")
{
    // "MAX_REQUESTS=2 bans after 3 identical requests" — rate-limit would be
    // the documented expectation; ban is the actual result.
    DetectorFixture fixture(
        makeConfig(/*maxRequests*/ 2, /*maxFingerprints*/ 100, std::chrono::seconds{60}, std::chrono::seconds{30}, std::chrono::seconds{3600}));

    REQUIRE(fixture.send(IP, "same-body") == DOSDetector::Status::ALLOWED);
    REQUIRE(fixture.send(IP, "same-body") == DOSDetector::Status::ALLOWED);
    // Third identical request → BANNED (not RATELIMITED as the README implies).
    CHECK(fixture.send(IP, "same-body") == DOSDetector::Status::BANNED);
}
