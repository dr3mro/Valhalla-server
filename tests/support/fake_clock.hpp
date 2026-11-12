#pragma once

#include <chrono>
#include <functional>

#include "gatekeeper/dosdetector/dosdetector.hpp"

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
