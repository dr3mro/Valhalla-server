#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "utils/threadsafequeue/threadsafequeue.hpp"

TEST_CASE("ThreadSafeQueue is FIFO", "[utils][queue]")
{
    ThreadSafeQueue<int> queue;

    queue.push(1);
    queue.push(2);
    queue.push(3);

    REQUIRE(queue.size() == 3);
    CHECK(queue.pop() == std::optional<int>(1));
    CHECK(queue.pop() == std::optional<int>(2));
    CHECK(queue.pop() == std::optional<int>(3));
    CHECK(queue.size() == 0);
}

// Bug 8 fixed: dead condition_variable (and its #include) removed. pop()
// was always non-blocking (returns nullopt); the CV was never waited on.
TEST_CASE("ThreadSafeQueue pop on an empty queue returns nullopt without blocking", "[utils][queue]")
{
    ThreadSafeQueue<std::string> queue;

    CHECK(queue.pop() == std::nullopt);
    CHECK(queue.empty());
}

TEST_CASE("ThreadSafeQueue front peeks without consuming", "[utils][queue]")
{
    ThreadSafeQueue<int> queue;

    CHECK(queue.front() == std::nullopt);

    queue.push(7);
    CHECK(queue.front() == std::optional<int>(7));
    CHECK(queue.size() == 1);  // peek did not consume
    CHECK(queue.pop() == std::optional<int>(7));
}

TEST_CASE("ThreadSafeQueue clear empties the queue", "[utils][queue]")
{
    ThreadSafeQueue<int> queue;

    for (int i = 0; i < 5; ++i)
    {
        queue.push(i);
    }
    CHECK_FALSE(queue.empty());

    queue.clear();
    CHECK(queue.empty());
    CHECK(queue.size() == 0);
    CHECK(queue.pop() == std::nullopt);
}

TEST_CASE("ThreadSafeQueue emplace constructs in place", "[utils][queue]")
{
    struct Point
    {
        int x;
        int y;
    };

    ThreadSafeQueue<Point> queue;
    queue.emplace(1, 2);
    queue.emplace(3, 4);

    const auto first = queue.pop();
    REQUIRE(first.has_value());
    CHECK(first->x == 1);
    CHECK(first->y == 2);

    const auto second = queue.pop();
    REQUIRE(second.has_value());
    CHECK(second->x == 3);
    CHECK(second->y == 4);
}

TEST_CASE("ThreadSafeQueue handles concurrent producers and consumers", "[utils][queue]")
{
    constexpr int kProducers      = 4;
    constexpr int kItemsPerThread = 250;

    ThreadSafeQueue<int> queue;

    std::vector<std::thread> producers;
    for (int t = 0; t < kProducers; ++t)
    {
        producers.emplace_back(
            [&queue, t]
            {
                for (int i = 0; i < kItemsPerThread; ++i)
                {
                    queue.push(t * kItemsPerThread + i);
                }
            });
    }

    std::vector<int> consumed;
    consumed.reserve(kProducers * kItemsPerThread);

    // Consume while the producers are still running; pop() is non-blocking,
    // so spin until everything has been consumed.
    while (consumed.size() < static_cast<size_t>(kProducers * kItemsPerThread))
    {
        while (auto value = queue.pop())
        {
            consumed.push_back(*value);
        }
    }

    for (auto& producer : producers)
    {
        producer.join();
    }

    // Drain whatever is left, then compare as sets.
    while (auto value = queue.pop())
    {
        consumed.push_back(*value);
    }

    std::vector<int> expected;
    expected.reserve(consumed.size());
    for (int t = 0; t < kProducers; ++t)
    {
        for (int i = 0; i < kItemsPerThread; ++i)
        {
            expected.push_back(t * kItemsPerThread + i);
        }
    }
    std::sort(expected.begin(), expected.end());
    std::sort(consumed.begin(), consumed.end());

    REQUIRE(consumed.size() == expected.size());
    CHECK(consumed == expected);
}

TEST_CASE("ThreadSafeQueue push rvalue moves the object in", "[utils][queue]")
{
    ThreadSafeQueue<std::string> queue;

    std::string s = "moved-string";
    queue.push(std::move(s));

    CHECK(queue.size() == 1);
    const auto val = queue.pop();
    REQUIRE(val.has_value());
    CHECK(*val == "moved-string");
}

TEST_CASE("ThreadSafeQueue size tracks insertions and removals", "[utils][queue]")
{
    ThreadSafeQueue<int> queue;

    CHECK(queue.size() == 0);
    queue.push(1);
    CHECK(queue.size() == 1);
    queue.push(2);
    CHECK(queue.size() == 2);
    queue.pop();
    CHECK(queue.size() == 1);
    queue.clear();
    CHECK(queue.size() == 0);
    CHECK(queue.empty());
}
