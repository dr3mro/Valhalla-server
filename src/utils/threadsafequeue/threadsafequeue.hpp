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

template <typename T>
void ThreadSafeQueue<T>::push(const T& value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(value);
    cv_.notify_one();
}

template <typename T>
void ThreadSafeQueue<T>::push(T&& value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(std::move(value));
    cv_.notify_one();
}

template <typename T>
std::optional<T> ThreadSafeQueue<T>::pop()
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (queue_.empty())
    {
        return std::nullopt;
    }
    T value = std::move(queue_.front());
    queue_.pop();
    return value;
}

template <typename T>
bool ThreadSafeQueue<T>::empty() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
}

template <typename T>
size_t ThreadSafeQueue<T>::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

template <typename T>
void ThreadSafeQueue<T>::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::queue<T>               empty;
    std::swap(queue_, empty);
}

template <typename T>
template <typename... Args>
void ThreadSafeQueue<T>::emplace(Args&&... args)
{
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.emplace(std::forward<Args>(args)...);
    cv_.notify_one();
}

template <typename T>
std::optional<T> ThreadSafeQueue<T>::front()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty())
    {
        return std::nullopt;
    }
    return queue_.front();
}
