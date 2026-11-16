/**
 * @file test_store.cpp
 * @brief Catch2 unit tests for Store, the process-wide service locator keyed by
 * C++ type.
 * @details Store hands out one shared object per type: getObject creates on
 * first use and returns the same instance later, replaceObject swaps in a
 * different instance (used to inject fakes in tests), and registerObject
 *          rejects duplicates. These tests cover each of those behaviours plus
 * a historical concurrency race.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Controllers call Store to obtain shared helpers such as the database client.
 * If getObject returned a fresh object every time, expensive resources would be
 * duplicated and state would diverge between components. If replacement did not
 * work, tests could not inject fakes. The concurrency test guards the "only one
 * instance per type" promise when several threads ask for the same type at
 * startup.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE / SECTION:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Catch2 assertion macros (CHECK, REQUIRE, CHECK_THROWS_AS):
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - Service locator pattern:
 * https://en.wikipedia.org/wiki/Service_locator_pattern
 * - std::shared_ptr / std::make_shared:
 * https://en.cppreference.com/w/cpp/memory/shared_ptr
 * - std::thread and join: https://en.cppreference.com/w/cpp/thread/thread/join
 * - typeid / std::type_index:
 * https://en.cppreference.com/w/cpp/types/type_index
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Header for atomic counters (not used directly by these tests).
#include <atomic>
// Brings in TEST_CASE, CHECK, REQUIRE and CHECK_THROWS_AS.
#include <catch2/catch_test_macros.hpp>
// std::shared_ptr and std::make_shared, used to hold Store objects.
#include <memory>
// std::runtime_error, the exception expected on double registration.
#include <stdexcept>
#include <thread>  // std::thread, used in the concurrency test.
// std::vector holds the worker threads and the result pointers.
#include <vector>

#include "store/store.hpp"  // The Store service locator under test.

// Each TEST_CASE that exercises Store must use a *distinct* type so that
// separate tests do not collide on the global Store::inventory.
// Define one throwaway type per test scenario inside an anonymous namespace.

namespace
{
  /**
   * @brief Throwaway type used only by the first getObject test.
   * @details Store keys its singletons by C++ type, so every test needs its
   * own type to avoid shared state.
   */
  struct StoreA
  {
    int val = 0;  // A single integer payload, default-initialised to zero.
    explicit StoreA(int v = 0) : val(v)
    {
    }  // Stores the supplied value; "explicit" prevents
       // accidental conversion.
  };

  /**
   * @brief Throwaway type used by the "same instance on repeated calls" test.
   */
  struct StoreB
  {
    std::string name;  // A text payload to show constructor arguments are kept.
    explicit StoreB(std::string n = "") : name(std::move(n))
    {
    }  // Moves the argument into the member to avoid a copy.
  };

  /**
   * @brief Throwaway type used by the replaceObject test.
   */
  struct StoreC
  {
    double x = 0.0;  // A double payload with a safe default of zero.
  };

  /**
   * @brief Throwaway type used by the double-registration test.
   */
  struct StoreD
  {
    int v = 42;  // A payload with a non-zero default so it is easy to
                 // recognise.
  };

  // Used for thread-safety test; each thread calls getObject<StoreRaceN>.
  /**
   * @brief Throwaway type for the concurrent first-access test.
   * @details Records the value passed to its constructor so the test can
   * reason about which instance was built.
   */
  struct StoreRace
  {
    int created = 0;  // Counts the constructor argument, defaulting to zero.
    explicit StoreRace(int v = 1) : created(v)
    {
    }  // Records the value it was given.
  };
}  // namespace

// First call for a type must construct the object with the given arguments and
// return it.
TEST_CASE("Store::getObject creates and returns an object on first call",
          "[utils][store]")
{
  auto obj = Store::getObject<StoreA>(
      7);  // Ask for a StoreA built with constructor argument 7.
  REQUIRE(
      obj !=
      nullptr);  // Must get a real pointer; stop the test here if it is null.
  CHECK(obj->val ==
        7);  // The constructor argument must have been passed through.
}

// Once a type is registered, later calls must return the very same object and
// ignore new arguments.
TEST_CASE("Store::getObject returns the same instance on repeated calls",
          "[utils][store]")
{
  auto first = Store::getObject<StoreB>(
      "hello");  // First call constructs StoreB with "hello".
  auto second = Store::getObject<StoreB>();  // already registered; args ignored
                                             // — the existing object is reused.

  REQUIRE(first != nullptr);           // The first pointer must be valid.
  REQUIRE(second != nullptr);          // The second pointer must be valid too.
  CHECK(first.get() == second.get());  // Comparing raw addresses proves one
                                       // object, not two separate copies.
  CHECK(first->name ==
        "hello");  // The original constructor argument is still stored.
}

// replaceObject must swap in a new instance, and later lookups must see the
// replacement.
TEST_CASE("Store::replaceObject swaps the registered instance",
          "[utils][store]")
{
  auto original = Store::getObject<StoreC>();  // Get the current singleton.
  original->x   = 3.14;                        // Give it a recognisable value.

  auto replacement =
      std::make_shared<StoreC>();  // Build a different object on the heap.
  replacement->x = 2.72;           // Give the replacement its own value.
  Store::replaceObject<StoreC>(
      replacement);  // Publish it as the new singleton for StoreC.

  auto fetched = Store::getObject<StoreC>();  // Ask for the type again.
  CHECK(fetched.get() ==
        replacement.get());   // The lookup must return our
                              // replacement, not the old object.
  CHECK(fetched->x == 2.72);  // ...carrying the replacement's data.

  // Restore original so other tests that might use StoreC are unaffected.
  Store::replaceObject<StoreC>(original);  // Put the original back to avoid
                                           // leaking state into other tests.
}

// registerObject is explicit and must refuse a type that is already in the
// inventory.
TEST_CASE("Store::registerObject throws on double registration",
          "[utils][store]")
{
  // StoreD has never been registered; first call succeeds.
  auto obj = Store::getObject<StoreD>();  // This lazily registers StoreD.
  REQUIRE(
      obj !=
      nullptr);  // Confirm the object exists before trying to register again.

  // A second explicit registerObject call must throw.
  CHECK_THROWS_AS(Store::registerObject<StoreD>(),
                  std::runtime_error);  // Passes only if that exact exception
                                        // type is thrown.
}

// Bug 11 (historical; fixed in commit bfb375e): getObject used separate find
// and registerObject calls, so concurrent first access could create two
// instances. The current implementation constructs outside the lock and
// re-checks under it, so this hidden test now passes. See tests/README.md
// §Bug 11.
TEST_CASE("Store concurrent first-access does not race",
          "[.known-bug][utils][store][concurrency]")
{
  // All 8 threads race to call getObject<StoreRace>(); only one must win the
  // construction; they all must receive the same pointer.
  constexpr int kThreads = 8;  // Compile-time constant: how many threads race.
  std::vector<std::shared_ptr<StoreRace>> results(
      kThreads);  // One slot per thread for the pointer it receives.

  std::vector<std::thread> workers;  // Will hold the racing threads.
  workers.reserve(kThreads);  // Reserve space so pushing threads does not
                              // reallocate the vector.
  for (int i = 0; i < kThreads; ++i)  // Launch eight threads.
  {
    workers.emplace_back(
        [&results, i]
        {
          results[i] = Store::getObject<StoreRace>();
        });  // Each writes its result into its own slot; i is captured by
             // value.
  }
  for (auto& w : workers)  // Walk the workers.
  {
    w.join();  // Wait until all threads finish before inspecting results.
  }

  // All threads must have received a non-null pointer.
  for (const auto& r : results)  // Check every slot.
  {
    REQUIRE(r != nullptr);  // A null result would mean getObject failed.
  }

  // All pointers must be identical (same singleton).
  const void* expected =
      results[0].get();  // Pick the first pointer as the reference address.
  for (const auto& r : results)  // Compare every other pointer against it.
  {
    CHECK(r.get() == expected);  // Identical addresses prove only one
                                 // object was created.
  }
}
