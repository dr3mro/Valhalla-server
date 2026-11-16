/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file fake_clock.hpp
 * @brief Fake clock objects that let tests control "now" instead of waiting for
 * real time to pass.
 * @details Two clocks are provided. ManualClock is a static clock that tests
 * can advance or reset, matching the interface the MemCache template expects.
 * FakeClock is an injectable clock whose callable can be handed to the DoS
 * detector, so time-based rules can be tested instantly and repeatably rather
 * than by sleeping.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Security features such as rate limiting and DoS detection depend on the
 * passage of time. Testing them with the real clock would be slow and
 * unreliable (a test could fail just because the machine was busy). By
 * controlling time directly, these tests pin down time-based behaviour
 * deterministically and catch regressions in the protections that keep the
 * server available.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - C++ chrono (time points and durations):
 * https://en.cppreference.com/w/cpp/chrono
 * - static class members (one value shared by all uses):
 * https://en.cppreference.com/w/cpp/language/static
 * - Lambdas (small inline functions):
 * https://en.cppreference.com/w/cpp/language/lambda
 * - Catch2 test cases and sections:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per translation unit.

#include <chrono>  // Provides time_point, duration, milliseconds and seconds.
// Provides std::function-style callables used by DOSDetector::ClockFn.
#include <functional>

// DOSDetector defines the ClockFn type we return.
#include "gatekeeper/dosdetector/dosdetector.hpp"

/**
 * A static clock whose current time is advanced manually.
 * Compatible with the MemCache<T, ClockT> template parameter (requires
 * ClockT::now() as a static method).
 * Reset to epoch zero before each test to avoid inter-test contamination.
 * @brief A clock made of static members, so every caller sees the same "now".
 */
struct ManualClock
{
  using duration = std::chrono::milliseconds;  // The smallest time step this
                                               // clock measures.
  using rep =
      duration::rep;  // The underlying integer type that stores a duration.
  using period =
      duration::period;  // The tick period (how many seconds one tick lasts).
  using time_point =
      std::chrono::time_point<ManualClock>;  // A point in time measured by
                                             // this clock.

  static constexpr bool is_steady =
      true;  // Claim steadiness: time only moves when a test says so.

  static time_point now() noexcept
  {
    return current_;
  }  // Return the current fake time; cannot throw.

  static void advance(std::chrono::milliseconds ms)
  {
    current_ += ms;
  }  // Move fake time forward by milliseconds.
  static void advance(std::chrono::seconds s)
  {
    current_ += s;
  }  // Move fake time forward by whole seconds.

  /**
   * Call at the start of each test so tests are independent.
   */
  static void reset()
  {
    current_ = time_point{};
  }  // Set fake time back to the epoch (zero).

 private:
  static inline time_point current_{};  // The single shared "now" value; inline
                                        // means one definition in the program.
};

/**
 * Manually advanced clock for testing time-dependent code (DoS detector).
 * No real waiting: tests decide when time passes.
 * @brief An object-based clock whose time can be advanced and read on demand.
 * @details It can produce a callable for DOSDetector so the detector reads this
 * object's time instead of the real system clock.
 */
class FakeClock
{
 public:
  using TimePoint = std::chrono::time_point<
      std::chrono::steady_clock>;  // Use the steady clock's time-point type
                                   // as a stand-in.

  FakeClock()
      : now_(std::chrono::steady_clock::now())
  {}  // Start at the real current time so values look realistic.

  void advance(const std::chrono::seconds seconds)
  {
    now_ += seconds;
  }  // Jump fake time forward without really waiting.

  [[nodiscard]] TimePoint now() const
  {
    return now_;
  }  // Return the fake time; [[nodiscard]] warns if the result is ignored.

  /**
   * Returns the callable to inject into DOSDetector.
   * The returned lambda captures `this` by reference, so this FakeClock must
   * outlive the detector (and any callbacks) that hold the ClockFn.
   */
  [[nodiscard]] DOSDetector::ClockFn callable()
  {
    return [this]
    {
      return now_;
    };  // The lambda reads this object's time whenever the detector asks.
  }

 private:
  TimePoint now_;  // The fake "now" value, advanced only by the test.
};
