#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <algorithm>

#include "configurator/configurator.hpp"
#include "configurator/defaults.hpp"
#include "configurator/envloader/envloader.hpp"
#include "store/store.hpp"

namespace
{
    /// EnvLoader exposes its helpers as protected members; this subclass
    /// re-exports them for direct testing.
    struct TestLoader : EnvLoader
    {
        using EnvLoader::getEnvironmentVariable;  // the template overload is public already
        using EnvLoader::optimize_performance;
        using EnvLoader::parseSet;
        void printValues() const override {}
    };

    /// RAII guard that sets an environment variable for the duration of a
    /// scope and restores the previous state afterwards.
    struct ScopedEnv
    {
        ScopedEnv(const std::string &key, const std::string &value) : key_(key)
        {
            const char *previous = std::getenv(key.c_str());
            hadPrevious_ = previous != nullptr;
            if (hadPrevious_)
            {
                previous_ = previous;
            }
            setenv(key.c_str(), value.c_str(), 1);
        }

        ~ScopedEnv()
        {
            if (hadPrevious_)
            {
                setenv(key_.c_str(), previous_.c_str(), 1);
            }
            else
            {
                unsetenv(key_.c_str());
            }
        }

        std::string key_;
        bool        hadPrevious_ = false;
        std::string previous_;
    };
}  // namespace

TEST_CASE("EnvLoader string overload returns override or default", "[configurator][envloader]")
{
    TestLoader loader;

    SECTION("missing variable yields the default")
    {
        CHECK(loader.getEnvironmentVariable("VALHALLA_TEST_DEFINITELY_UNSET", std::string("fallback")) == "fallback");
    }

    SECTION("present variable overrides the default")
    {
        ScopedEnv guard("VALHALLA_TEST_DEFINITELY_UNSET", "override");
        CHECK(loader.getEnvironmentVariable("VALHALLA_TEST_DEFINITELY_UNSET", std::string("fallback")) == "override");
    }
}

TEST_CASE("EnvLoader bool overload parses 'true' strictly", "[configurator][envloader]")
{
    TestLoader loader;

    SECTION("missing variable yields the default")
    {
        CHECK(loader.getEnvironmentVariable("VALHALLA_TEST_DEFINITELY_UNSET", true));
        CHECK_FALSE(loader.getEnvironmentVariable("VALHALLA_TEST_DEFINITELY_UNSET", false));
    }

    SECTION("value 'true' maps to true")
    {
        ScopedEnv guard("VALHALLA_TEST_DEFINITELY_UNSET", "true");
        CHECK(loader.getEnvironmentVariable("VALHALLA_TEST_DEFINITELY_UNSET", false));
    }

    SECTION("value 'false' maps to false regardless of the default")
    {
        ScopedEnv guard("VALHALLA_TEST_DEFINITELY_UNSET", "false");
        CHECK_FALSE(loader.getEnvironmentVariable("VALHALLA_TEST_DEFINITELY_UNSET", true));
    }

    SECTION("any other non-empty value maps to false")
    {
        // getEnvironmentVariable(bool) compares against the literal "true";
        // everything else (including "1" and "TRUE") is false.
        ScopedEnv guard("VALHALLA_TEST_DEFINITELY_UNSET", "1");
        CHECK_FALSE(loader.getEnvironmentVariable("VALHALLA_TEST_DEFINITELY_UNSET", true));
    }
}

TEST_CASE("EnvLoader chrono-seconds overload parses integer seconds", "[configurator][envloader]")
{
    TestLoader loader;

    SECTION("missing variable yields the default")
    {
        CHECK(loader.getEnvironmentVariable("VALHALLA_TEST_DEFINITELY_UNSET", std::chrono::seconds{7}) == std::chrono::seconds{7});
    }

    SECTION("present variable is parsed")
    {
        ScopedEnv guard("VALHALLA_TEST_DEFINITELY_UNSET", "42");
        CHECK(loader.getEnvironmentVariable("VALHALLA_TEST_DEFINITELY_UNSET", std::chrono::seconds{7}) == std::chrono::seconds{42});
    }
}

TEST_CASE("EnvLoader integer template overload uses std::stoi", "[configurator][envloader]")
{
    TestLoader loader;

    SECTION("missing variable yields the default")
    {
        CHECK(loader.getEnvironmentVariable<int>("VALHALLA_TEST_DEFINITELY_UNSET", 3) == 3);
    }

    SECTION("present variable is parsed with stoi")
    {
        ScopedEnv guard("VALHALLA_TEST_DEFINITELY_UNSET", "17");
        CHECK(loader.getEnvironmentVariable<int>("VALHALLA_TEST_DEFINITELY_UNSET", 3) == 17);
    }
}

// Known bug 10: getEnvironmentVariable<T>() calls std::stoi / std::stoull
// without catching exceptions. A malformed env var (non-numeric, overflow)
// propagates an unhandled exception that aborts server startup instead of
// falling back to the default. The stoull variant also silently wraps on
// negative numbers.
// Fix: wrap the stoi/stoull call in a try/catch; return the default on error.
TEST_CASE("EnvLoader integer overload throws on non-numeric input (known bug 10)", "[configurator][envloader][.known-bug]")
{
    TestLoader loader;

    SECTION("non-numeric value throws instead of falling back to the default")
    {
        ScopedEnv guard("VALHALLA_TEST_DEFINITELY_UNSET", "not-a-number");
        REQUIRE_THROWS_AS((loader.getEnvironmentVariable<int>("VALHALLA_TEST_DEFINITELY_UNSET", 3)), std::exception);
    }
}

TEST_CASE("EnvLoader parseSet splits on commas and trims whitespace", "[configurator][envloader]")
{
    TestLoader loader;

    CHECK(loader.parseSet("a,b,c") == std::unordered_set<std::string>{"a", "b", "c"});
    CHECK(loader.parseSet(" a , b ") == std::unordered_set<std::string>{"a", "b"});
    CHECK(loader.parseSet("single") == std::unordered_set<std::string>{"single"});
    CHECK(loader.parseSet("").empty());
}

// Known bug 9: parseSet("a,,b") inserts an empty string as a member of the
// resulting set. An empty regex pattern matches everything, so this would
// effectively whitelist or blacklist *all* IP addresses.
// Fix: skip empty tokens after splitting on commas.
TEST_CASE("EnvLoader parseSet produces an empty-string entry from consecutive commas", "[configurator][envloader][.known-bug]")
{
    TestLoader loader;

    const auto parsed = loader.parseSet("a,,b");
    CHECK(parsed.count("a") == 1);
    CHECK(parsed.count("b") == 1);
    CHECK(parsed.count("") == 1);  // empty string present — the bug
}

TEST_CASE("EnvLoader set-of-strings overload falls back to an empty set", "[configurator][envloader]")
{
    TestLoader loader;

    const auto unset = loader.getEnvironmentVariable("VALHALLA_TEST_DEFINITELY_UNSET");
    CHECK(unset.empty());

    ScopedEnv guard("VALHALLA_TEST_DEFINITELY_UNSET", "10.0.0.1,10.0.0.2");
    const auto parsed = loader.getEnvironmentVariable("VALHALLA_TEST_DEFINITELY_UNSET");
    CHECK(parsed == std::unordered_set<std::string>{"10.0.0.1", "10.0.0.2"});
}

TEST_CASE("EnvVars get reads the process environment", "[configurator][envloader]")
{
    struct Getter : EnvVars
    {
        using EnvVars::get;
    };

    Getter getter;

    CHECK_FALSE(getter.get("VALHALLA_TEST_DEFINITELY_UNSET").has_value());

    ScopedEnv guard("VALHALLA_TEST_DEFINITELY_UNSET", "hello");
    CHECK(getter.get("VALHALLA_TEST_DEFINITELY_UNSET") == std::optional<std::string>("hello"));
}

// Known bug 5: EnvVars::set() calls putenv(envVar.c_str()) where envVar is a
// function-local std::string. putenv() keeps only the pointer; after set()
// returns the string is destroyed, leaving a dangling pointer in the process
// environment (UB on the next getenv / putenv call).
// Fix: use setenv(key, value, 1) (POSIX) or keep the string alive in a static
// / heap-allocated buffer.
TEST_CASE("EnvVars::set has a dangling-pointer bug (not called; UB on return)", "[configurator][envloader][.known-bug]")
{
    // Calling EnvVars::set() would immediately exhibit UB; we document the
    // bug here without triggering it.
    SUCCEED("Bug documented: see tests/README.md known bug 5 and envvars.cpp:EnvVars::set");
}

TEST_CASE("EnvLoader optimize_performance raises sub-1 values to ncpus*factor", "[configurator][envloader]")
{
    TestLoader loader;

    SECTION("zero is raised")
    {
        uint16_t value = 0;
        loader.optimize_performance(value, 5);
        const auto ncpus = std::max<uint16_t>(1, static_cast<uint16_t>(std::thread::hardware_concurrency()));
        CHECK(value == ncpus * 5);
    }

    SECTION("a positive value is left alone")
    {
        uint16_t value = 3;
        loader.optimize_performance(value, 5);
        CHECK(value == 3);
    }
}

TEST_CASE("Configurator exposes the configured singleton through Store", "[configurator][configurator]")
{
    auto configurator = Store::getObject<Configurator>();
    REQUIRE(configurator != nullptr);

    SECTION("DOSDetectorConfig uses the compiled-in defaults (env unset)")
    {
        const auto &config = configurator->get<Configurator::DOSDetectorConfig>();
        CHECK(config.max_requests == Defaults::DosDetector::MAX_REQUESTS_);
        CHECK(config.max_fingerprints == Defaults::DosDetector::MAX_FPS_);
        CHECK(config.period == std::chrono::seconds{Defaults::DosDetector::PERIOD_});
        CHECK(config.ratelimit_duration == std::chrono::seconds{Defaults::DosDetector::RL_DURATION_});
        CHECK(config.ban_duration == std::chrono::seconds{Defaults::DosDetector::BAN_DURATION_});
        CHECK(config.whitelist.empty());
        CHECK(config.blacklist.empty());
    }

    SECTION("DatabaseConfig uses the compiled-in defaults (env unset)")
    {
        const auto &config = configurator->get<Configurator::DatabaseConfig>();
        CHECK(config.host == Defaults::Database::DB_HOST_);
        CHECK(config.port == Defaults::Database::DB_PORT_);
        CHECK(config.name == Defaults::Database::DB_NAME_);
        CHECK(config.user == Defaults::Database::DB_USER_);
        CHECK(config.pass == Defaults::Database::DB_PASS_);
        CHECK_FALSE(config.ssl);
        CHECK(config.max_conn == Defaults::Database::DB_MAX_CONN_);
    }

    SECTION("TokenManagerParameters come from the test environment")
    {
        // tests/support/global_env.cpp sets TOKEN_* before the Configurator
        // singleton was created; the values must have been picked up there.
        const auto &config = configurator->get<Configurator::TokenManagerParameters>();
        CHECK(std::string(config.secret) == "valhalla-unit-test-secret-key");
        CHECK(std::string(config.issuer) == "valhalla-unit-test-issuer");
        CHECK(std::string(config.type) == "JWS");
        CHECK(config.validity == 60);
    }
}
