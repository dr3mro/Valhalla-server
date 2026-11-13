#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include "store/store.hpp"

// Each TEST_CASE that exercises Store must use a *distinct* type so that
// separate tests do not collide on the global Store::inventory.
// Define one throwaway type per test scenario inside an anonymous namespace.

namespace
{
    struct StoreA
    {
        int val = 0;
        explicit StoreA(int v = 0) : val(v) {}
    };

    struct StoreB
    {
        std::string name;
        explicit StoreB(std::string n = "") : name(std::move(n)) {}
    };

    struct StoreC
    {
        double x = 0.0;
    };

    struct StoreD
    {
        int v = 42;
    };

    // Used for thread-safety test; each thread calls getObject<StoreRaceN>.
    struct StoreRace
    {
        int created = 0;
        explicit StoreRace(int v = 1) : created(v) {}
    };
}  // namespace

TEST_CASE("Store::getObject creates and returns an object on first call", "[utils][store]")
{
    auto obj = Store::getObject<StoreA>(7);
    REQUIRE(obj != nullptr);
    CHECK(obj->val == 7);
}

TEST_CASE("Store::getObject returns the same instance on repeated calls", "[utils][store]")
{
    auto first  = Store::getObject<StoreB>("hello");
    auto second = Store::getObject<StoreB>();  // already registered; args ignored

    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first.get() == second.get());
    CHECK(first->name == "hello");
}

TEST_CASE("Store::replaceObject swaps the registered instance", "[utils][store]")
{
    auto original = Store::getObject<StoreC>();
    original->x   = 3.14;

    auto replacement = std::make_shared<StoreC>();
    replacement->x   = 2.72;
    Store::replaceObject<StoreC>(replacement);

    auto fetched = Store::getObject<StoreC>();
    CHECK(fetched.get() == replacement.get());
    CHECK(fetched->x == 2.72);

    // Restore original so other tests that might use StoreC are unaffected.
    Store::replaceObject<StoreC>(original);
}

TEST_CASE("Store::registerObject throws on double registration", "[utils][store]")
{
    // StoreD has never been registered; first call succeeds.
    auto obj = Store::getObject<StoreD>();
    REQUIRE(obj != nullptr);

    // A second explicit registerObject call must throw.
    CHECK_THROWS_AS(Store::registerObject<StoreD>(), std::runtime_error);
}

// Known bug: Store::getObject has a TOCTOU race between the inventory lookup
// and registerObject insertion. Two threads can both fail the find() check
// before either inserts, so both call registerObject and create distinct
// objects. This test documents the broken behaviour.
// Fix: hold the lock across the check-and-insert in getObject, or use a
// double-checked locking pattern that is correct (single lock acquisition).
TEST_CASE("Store concurrent first-access does not race", "[utils][store][concurrency][.known-bug][!shouldfail]")
{
    // All 8 threads race to call getObject<StoreRace>(); only one must win the
    // construction; they all must receive the same pointer.
    constexpr int                           kThreads = 8;
    std::vector<std::shared_ptr<StoreRace>> results(kThreads);

    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i)
    {
        workers.emplace_back([&results, i] { results[i] = Store::getObject<StoreRace>(); });
    }
    for (auto& w : workers)
    {
        w.join();
    }

    // All threads must have received a non-null pointer.
    for (const auto& r : results)
    {
        REQUIRE(r != nullptr);
    }

    // All pointers must be identical (same singleton).
    const void* expected = results[0].get();
    for (const auto& r : results)
    {
        CHECK(r.get() == expected);
    }
}
