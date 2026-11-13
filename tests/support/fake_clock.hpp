#pragma once

#include <chrono>
#include <functional>

#include "gatekeeper/dosdetector/dosdetector.hpp"

/// A static clock whose current time is advanced manually.
/// Compatible with the MemCache<T, ClockT> template parameter (requires
/// ClockT::now() as a static method).
/// Reset to epoch zero before each test to avoid inter-test contamination.
struct ManualClock
{
    using duration   = std::chrono::milliseconds;
    using rep        = duration::rep;
    using period     = duration::period;
    using time_point = std::chrono::time_point<ManualClock>;

    static constexpr bool is_steady = true;

    static time_point now() noexcept { return current_; }

    static void advance(std::chrono::milliseconds ms) { current_ += ms; }
    static void advance(std::chrono::seconds s) { current_ += s; }

    /// Call at the start of each test so tests are independent.
    static void reset() { current_ = time_point{}; }

   private:
    static inline time_point current_{};
};

/// Manually advanced clock for testing time-dependent code (DoS detector).
/// No real waiting: tests decide when time passes.
class FakeClock
{
   public:
    using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;

    FakeClock() : now_(std::chrono::steady_clock::now()) {}

    void advance(const std::chrono::seconds seconds) { now_ += seconds; }

    [[nodiscard]] TimePoint now() const { return now_; }

    /// Returns the callable to inject into DOSDetector.
    [[nodiscard]] DOSDetector::ClockFn callable()
    {
        return [this] { return now_; };
    }

   private:
    TimePoint now_;
};
