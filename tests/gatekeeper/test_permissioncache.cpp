#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <jsoncons/json.hpp>

#include "gatekeeper/permissionmanager/permissioncache.hpp"
#include "support/fake_clock.hpp"

namespace
{
    using TestCache = BasicPermissionCache<ManualClock>;
    const jsoncons::json kPerm = jsoncons::json::parse(R"({"owner_id":1,"admin_id":2,"staff":{}})");
}  // namespace

TEST_CASE("PermissionCache miss then hit", "[gatekeeper][permissioncache]")
{
    ManualClock::reset();
    TestCache cache(std::chrono::seconds{30});

    CHECK_FALSE(cache.get("q1").has_value());
    cache.put("q1", kPerm, cache.generation());
    auto hit = cache.get("q1");
    REQUIRE(hit.has_value());
    CHECK(hit->at("owner_id").as<int>() == 1);
}

TEST_CASE("PermissionCache entries expire after the idle TTL", "[gatekeeper][permissioncache]")
{
    ManualClock::reset();
    TestCache cache(std::chrono::seconds{300});
    cache.put("q1", kPerm, cache.generation());

    ManualClock::advance(std::chrono::seconds{299});
    CHECK(cache.get("q1").has_value());
    ManualClock::advance(std::chrono::seconds{301});
    CHECK_FALSE(cache.get("q1").has_value());
}

TEST_CASE("PermissionCache use renews the idle timer", "[gatekeeper][permissioncache]")
{
    ManualClock::reset();
    TestCache cache(std::chrono::seconds{300});
    cache.put("q1", kPerm, cache.generation());

    for (int i = 0; i < 5; ++i)
    {
        ManualClock::advance(std::chrono::seconds{200});
        REQUIRE(cache.get("q1").has_value());
    }
    ManualClock::advance(std::chrono::seconds{301});
    CHECK_FALSE(cache.get("q1").has_value());
}

TEST_CASE("PermissionCache sweeps idle entries that are never read again", "[gatekeeper][permissioncache]")
{
    ManualClock::reset();
    TestCache cache(std::chrono::seconds{300});
    cache.put("idle", kPerm, cache.generation());

    ManualClock::advance(std::chrono::seconds{400});
    cache.put("fresh", kPerm, cache.generation());
    CHECK(cache.size() == 1);
}

TEST_CASE("PermissionCache invalidateAll drops every entry", "[gatekeeper][permissioncache]")
{
    ManualClock::reset();
    TestCache cache(std::chrono::seconds{30});
    cache.put("q1", kPerm, cache.generation());
    cache.put("q2", kPerm, cache.generation());

    cache.invalidateAll();
    CHECK_FALSE(cache.get("q1").has_value());
    CHECK_FALSE(cache.get("q2").has_value());
    CHECK(cache.size() == 0);
}

TEST_CASE("PermissionCache drops a put that raced with an invalidation", "[gatekeeper][permissioncache]")
{
    ManualClock::reset();
    TestCache cache(std::chrono::seconds{30});

    const auto generation = cache.generation();  // reader starts
    cache.invalidateAll();                       // writer commits meanwhile
    cache.put("q1", kPerm, generation);          // reader stores stale data

    CHECK_FALSE(cache.get("q1").has_value());
}

TEST_CASE("PermissionCache with TTL 0 is disabled", "[gatekeeper][permissioncache]")
{
    ManualClock::reset();
    TestCache cache(std::chrono::seconds{0});
    cache.put("q1", kPerm, cache.generation());
    CHECK_FALSE(cache.enabled());
    CHECK_FALSE(cache.get("q1").has_value());
}
