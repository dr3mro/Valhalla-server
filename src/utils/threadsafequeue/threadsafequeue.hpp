#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

template <typename T>
class ThreadSafeQueue
{
   public:
    ThreadSafeQueue()  = default;
    ~ThreadSafeQueue() = default;

    // Add an element to the queue
    void push(const T& value);
    void push(T&& value);

    // Retrieve and remove an element from the front of the queue
    std::optional<T> pop();

    // Check if the queue is empty
    bool empty() const;

    // Get the size of the queue
    size_t size() const;

    // Clear the queue
    void clear();

    // emplace to the queue
    template <typename... Args>
    void emplace(Args&&... args);

    // get front element from the queue
    std::optional<T> front();

   private:
    mutable std::mutex      mutex_;
    std::condition_variable cv_;
    std::queue<T>           queue_;
};

#include "utils/threadsafequeue/threadsafequeue.tpp"
