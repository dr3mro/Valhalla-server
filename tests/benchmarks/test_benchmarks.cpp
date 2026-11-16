/**
 * @file test_benchmarks.cpp
 * @brief Opt-in performance benchmarks for the thread-safe queue, the cache and
 * CORS header building.
 * @details These tests are hidden from normal runs by the [.benchmark] tag and
 * also SKIP unless the VALHALLA_RUN_BENCHMARKS environment variable is set, so
 * ordinary CI stays fast. They measure raw throughput with hand-written timing
 * loops and use Catch2's BENCHMARK macro for CORS. Global operator new is
 * replaced so allocations inside CORS code can be counted per call.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * CORS headers are added to every response, and the queue/cache are hot paths,
 * so a performance regression there would affect the whole server. These
 * benchmarks let developers compare changes against a baseline and catch
 * accidental slow-downs or extra allocations before they ship.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 benchmarks (the BENCHMARK macro):
 * https://github.com/catchorg/Catch2/blob/devel/docs/benchmarks.md
 * - Catch2 hidden tests ([.tag]) and SKIP:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Replacing global operator new:
 * https://en.cppreference.com/w/cpp/memory/new/operator_new
 * - std::atomic (values safe across threads):
 * https://en.cppreference.com/w/cpp/atomic/atomic
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#include <drogon/HttpRequest.h>   // Request objects used to drive Cors::apply.
#include <drogon/HttpResponse.h>  // Response objects created in the benchmarks.
#include <drogon/HttpTypes.h>     // HTTP methods such as Get and Options.

#include <atomic>  // Atomic counters/flags used by the allocation counter.
#include <catch2/benchmark/catch_benchmark.hpp>  // Catch2's BENCHMARK macro.
// Catch2's TEST_CASE, SKIP, INFO and CHECK macros.
#include <catch2/catch_test_macros.hpp>
#include <chrono>   // Steady clock used for manual timing.
#include <cstdlib>  // std::getenv and std::malloc / std::free.
#include <new>      // operator new overloads and std::bad_alloc / std::nothrow.
#include <string>   // std::string / std::to_string for cache keys.
#include <vector>   // Vector support for callers of this code.

#include "server/cors.hpp"  // Cors::apply, one of the benchmarked operations.
// MemCache, the expiring cache being measured.
#include "utils/memcache/memcache.hpp"
// ThreadSafeQueue, the queue being measured.
#include "utils/threadsafequeue/threadsafequeue.hpp"

// Opt-in benchmarks: the [.benchmark] tag hides these by default and they
// SKIP unless VALHALLA_RUN_BENCHMARKS is set, so normal CI runs stay fast.
// Anonymous namespace keeps the helpers private to this file.
namespace {
  /** @brief Gate: benchmarks only run when explicitly requested via env var.
   */
  bool benchmarksRequested() {
    return std::getenv("VALHALLA_RUN_BENCHMARKS") != nullptr;
  }  // Presence of the variable is enough; its value is ignored.

  // Global allocation counter driven by the overridden operator new below.
  // It is switched on only around the measured calls to keep overhead local.
  std::atomic<bool> g_countAllocations{
      false};  // Whether allocations should currently be counted.
  std::atomic<std::size_t> g_allocationCount{
      0};  // Running total of allocations counted.

  /** @brief Shared backend for every global operator new overload: count the
   * allocation when enabled, then allocate with malloc. */
  // Allocates memory and, optionally, counts the call.
  void* countedAllocate(std::size_t size) {
    // Count only when a benchmark
    // enabled counting.
    if (g_countAllocations.load(std::memory_order_relaxed)) {
      g_allocationCount
          .fetch_add(1,
                     std::memory_order_relaxed);  // Add one to the count
                                                  // without heavy ordering
                                                  // guarantees.
    }
    // Do the actual allocation with malloc.
    if (void* ptr = std::malloc(size)) {
      return ptr;  // Give the memory back to the caller.
    }
    throw std::bad_alloc();  // Throw the standard exception if allocation
                             // failed.
  }

  /** @brief Times `iterations` calls to `callable`, returning whole ms.
   * Used by the manual (non-Catch2) throughput benchmarks below. */
  // Runs a callable many times and
  // returns elapsed milliseconds.
  template <typename Callable>
  long long measure(Callable&& callable, const int iterations) {
    const auto start = std::chrono::steady_clock::now();  // Record the start on
                                                          // a monotonic clock.
    // Repeat the measured operation
    // the requested number of times.
    for (int i = 0; i < iterations; ++i) {
      callable(i);  // Pass the index so callers can vary each iteration.
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start)
        .count();  // Return elapsed whole milliseconds.
  }
}  // namespace

// Replace the global allocation functions so allocations made inside
// Cors::apply can be counted. Matching delete overloads free with std::free to
// stay paired with countedAllocate's malloc.
void* operator new(std::size_t size) {
  return countedAllocate(size);
}  // Ordinary single-object new.
void* operator new(std::size_t size, std::align_val_t) {
  return countedAllocate(size);
}  // Aligned single-object new; alignment is handled by malloc.
// Non-throwing new: return
// null instead of throwing.
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  try {
    // Try the normal counted allocation.
    // Catch any exception (normally bad_alloc).
    return countedAllocate(size);
  } catch (...) {
    return nullptr;  // The nothrow contract requires null, not an
                     // exception.
  }
}
// Non-throwing aligned new.
void* operator new(std::size_t size,
                   std::align_val_t,
                   const std::nothrow_t&) noexcept {
  try {
    // Try the normal counted allocation.
    // Catch any exception.
    return countedAllocate(size);
  } catch (...) {
    return nullptr;  // Honour the nothrow contract.
  }
}
void* operator new[](std::size_t size) {
  return countedAllocate(size);
}  // Ordinary array new.
void* operator new[](std::size_t size, std::align_val_t) {
  return countedAllocate(size);
}  // Aligned array new.

void operator delete(void* ptr) noexcept {
  std::free(ptr);
}  // Free memory from ordinary new.
void operator delete(void* ptr, std::size_t) noexcept {
  std::free(ptr);
}  // Sized delete; size is ignored.
void operator delete(void* ptr, std::align_val_t) noexcept {
  std::free(ptr);
}  // Aligned delete; alignment is ignored.
void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept {
  std::free(ptr);
}  // Sized aligned delete.
void operator delete[](void* ptr) noexcept {
  std::free(ptr);
}  // Free memory from array new.
void operator delete[](void* ptr, std::size_t) noexcept {
  std::free(ptr);
}  // Sized array delete.
void operator delete[](void* ptr, std::align_val_t) noexcept {
  std::free(ptr);
}  // Aligned array delete.
void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept {
  std::free(ptr);
}  // Sized aligned array delete.

// Throughput of the lock-based queue: push N items, then drain them.
// Hidden by the leading dot in the tag.
TEST_CASE("Benchmark: ThreadSafeQueue push/pop round trips", "[.benchmark]") {
  // Check the opt-in gate.
  if (!benchmarksRequested()) {
    SKIP();  // Skip without failing when benchmarks were not requested.
  }

  ThreadSafeQueue<int> queue;  // The queue under test.
  const auto           milliseconds = measure(
      [&](int i) {
        queue.push(i);
      },
      100000);  // Time 100000 pushes.

  // Will hold the drain time.
  long long popMs = 0;
  {
    const auto start =
        std::chrono::steady_clock::now();  // Start timing the drain.
    // Keep popping until the queue reports
    // it is empty.
    while (queue.pop().has_value()) {
    }
    popMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start)
                .count();  // Convert elapsed time to milliseconds.
  }

  INFO("push 100000 ints: " << milliseconds << " ms; drain: " << popMs
                            << " ms");  // Print the numbers if the case fails.
  CHECK(queue.empty());                 // Prove every pushed item was popped.
}

// Throughput of the expiring cache's insert and get paths.
// Cache throughput.
TEST_CASE("Benchmark: MemCache insert/get round trips", "[.benchmark]") {
  // Opt-in gate.
  if (!benchmarksRequested()) {
    SKIP();  // Skip unless explicitly requested.
  }

  MemCache<std::string>
      cache(100000,
            std::chrono::seconds{
                60});  // Cache with a large capacity and a 60-second lifetime.

  const auto insertMs = measure(
      [&](int i) {
        cache.insert("k" + std::to_string(i), "v" + std::to_string(i));
      },
      50000);  // Time 50000 inserts.

  const auto getMs = measure(
      [&](int i) {
        (void)cache.get("k" + std::to_string(i % 50000));
      },
      50000);  // Time 50000 gets over the inserted keys.

  INFO("insert 50000: " << insertMs << " ms; get 50000: " << getMs
                        << " ms");  // Report the timings.
  CHECK(insertMs >= 0);  // Sanity check: timing produced a non-negative value.
  CHECK(getMs >= 0);     // Sanity check for the get measurement.
}

// Compares Cors::apply (GET and preflight) against the old five-addHeader
// approach and bare response creation, and counts allocations per call to
// verify the header-setting path does not regress.
// CORS timing and allocation comparison.
TEST_CASE("Benchmark: Cors::apply", "[.benchmark]") {
  // Opt-in gate.
  if (!benchmarksRequested()) {
    SKIP();  // Skip unless explicitly requested.
  }

  auto requestGet =
      drogon::HttpRequest::newHttpRequest();  // A normal GET request.
  requestGet->setMethod(
      drogon::Get);  // Set the method so Cors takes the normal path.

  auto requestPreflight =
      drogon::HttpRequest::newHttpRequest();  // A preflight request.
  requestPreflight->setMethod(
      drogon::Options);  // OPTIONS marks it as preflight.
  requestPreflight
      ->addHeader("Access-Control-Request-Headers",
                  "content-type, x-custom");  // Supply headers to be echoed.

  // Catch2-measured normal-path benchmark.
  BENCHMARK("Cors::apply GET") {
    auto response = drogon::HttpResponse::newHttpResponse();  // Fresh response.
    Cors::apply(requestGet, response);  // Add CORS headers.
    return response;  // Return it so the compiler cannot optimise the work
                      // away.
  };

  // Catch2-measured preflight benchmark.
  BENCHMARK("Cors::apply preflight") {
    auto response = drogon::HttpResponse::newHttpResponse();  // Fresh response.
    Cors::apply(requestPreflight, response);  // Add preflight CORS headers.
    return response;                          // Return the response.
  };

  // Baseline: the previous
  // manual approach.
  BENCHMARK("old behaviour, five addHeader calls") {
    auto response = drogon::HttpResponse::newHttpResponse();  // Fresh response.
    response->addHeader("Access-Control-Allow-Origin",
                        "*");  // Baseline header 1.
    response->addHeader("Access-Control-Allow-Methods",
                        // Baseline header 2.
                        "GET, POST, PUT, PATCH, DELETE, HEAD, OPTIONS");
    response->addHeader("Access-Control-Expose-Headers",
                        "Retry-After, Content-Length");  // Baseline header 3.
    response
        ->addHeader("Access-Control-Allow-Headers",
                    // Baseline header 4.
                    "Content-Type, Authorization, Accept, X-Requested-With");
    response->addHeader("Access-Control-Max-Age",
                        "7200");  // Baseline header 5.
    return response;              // Return the response.
  };

  BENCHMARK("response creation only") {
    return drogon::HttpResponse::newHttpResponse();
  };  // Baseline cost of making a response alone.

  // Helper: counts average allocations per call of `fn`.
  auto countAllocations = [&](auto&& fn) {
    g_allocationCount
        .store(0, std::memory_order_relaxed);  // Reset the allocation counter.
    g_countAllocations.store(true,
                             std::memory_order_relaxed);  // Turn counting on.
    for (int i = 0; i < 10000;
         // Repeat enough times to get a stable average.
         ++i) {
      fn();  // Run the measured operation.
    }
    g_countAllocations
        .store(false, std::memory_order_relaxed);  // Turn counting off again.
    return g_allocationCount.load(std::memory_order_relaxed) /
           10000;  // Return allocations per single call.
  };

  WARN("Cors::apply GET allocations/call: "
       << countAllocations(  // Report average allocations for the normal path.
              [&]() {
                auto response =
                    drogon::HttpResponse::newHttpResponse();  // Fresh response
                                                              // per iteration.
                Cors::apply(requestGet, response);  // Add CORS headers.
              }));
  WARN("Cors::apply preflight allocations/call: "
       << countAllocations(  // Report average allocations for preflight.
              [&]() {
                auto response =
                    drogon::HttpResponse::newHttpResponse();  // Fresh response
                                                              // per iteration.
                Cors::apply(requestPreflight,
                            response);  // Add preflight headers.
              }));
  WARN(
      "old behaviour, five addHeader calls allocations/call: "
      << countAllocations(  // Allocation baseline of the old approach.
             [&]() {
               auto response =
                   drogon::HttpResponse::newHttpResponse();  // Fresh response
                                                             // per iteration.
               response->addHeader(
                   // Header 1.
                   "Access-Control-Allow-Origin",
                   "*");
               response
                   ->addHeader("Access-Control-Allow-Methods",
                               // Header 2.
                               "GET, POST, PUT, PATCH, DELETE, HEAD, OPTIONS");
               response->addHeader("Access-Control-Expose-Headers",
                                   "Retry-After, Content-Length");  // Header 3.
               response->addHeader("Access-Control-Allow-Headers",
                                   "Content-Type, Authorization, Accept, "
                                   "X-Requested-With");  // Header 4.
               response->addHeader("Access-Control-Max-Age",
                                   "7200");  // Header 5.
             }));
  WARN("response creation only allocations/call: " << countAllocations([&]() {
         (void)drogon::HttpResponse::newHttpResponse();
       }));  // Allocation baseline of response creation.
}
