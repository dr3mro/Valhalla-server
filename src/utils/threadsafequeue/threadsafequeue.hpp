/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file threadsafequeue.hpp
 * @brief Mutex-protected FIFO queue with non-blocking accessors.
 * @details Defines a template ThreadSafeQueue<T> that wraps std::queue with a
 *          single mutex, so several threads can push and pop without corrupting
 *          it. push adds items, pop removes and returns the front item, and
 *          empty/size/clear/front provide inspection. pop and front never wait:
 *          if the queue is empty they return std::nullopt immediately. front
 *          returns a copy, so the caller keeps a valid value after the lock is
 *          released.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Producers (for example worker threads or network callbacks) and consumers
 * often run at the same time. This queue lets them hand items over safely
 * without any locking code at each call site. Because it is a small reusable
 * template, any part of the server can create a thread-safe queue of any
 * copyable type. Without it, each producer/consumer pairing would need its own
 * error-prone locking.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - std::mutex and std::lock_guard:
 * https://en.cppreference.com/w/cpp/thread/lock_guard
 * - std::unique_lock: https://en.cppreference.com/w/cpp/thread/unique_lock
 * - std::optional: https://en.cppreference.com/w/cpp/utility/optional
 * - Perfect forwarding (std::forward):
 * https://en.cppreference.com/w/cpp/utility/forward
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per translation unit.

// Provides std::mutex, std::lock_guard and std::unique_lock used to protect the
// queue.
#include <mutex>
// std::optional is the return type that can hold a value or "nothing".
#include <optional>
// The underlying std::queue container that stores the elements.
#include <queue>

/**
 * @class ThreadSafeQueue
 * @brief FIFO container safe for concurrent producers and consumers.
 * @details Every operation takes a single mutex, so the queue can be shared
 *          between threads without external synchronisation. pop() and front()
 *          return std::nullopt immediately when empty: there is no condition
 *          variable, so callers never wait for an element to arrive. front()
 *          returns a copy rather than a reference, keeping the result valid
 *          after the lock is released.
 * @tparam T Element type; must be copyable (front) and movable (pop, rvalue
 * push).
 * @note empty() and size() report a snapshot; another thread can modify the
 *       queue as soon as the call returns.
 */
template <typename T>  // T is the type of element stored; the same code works
                       // for any copyable/movable type.
class ThreadSafeQueue  // A reusable wrapper that adds
                       // thread safety to std::queue.
{
 public:
  /**
   * @brief Default construction; the queue starts empty.
   */
  ThreadSafeQueue() =
      default;  // Default constructor builds an empty underlying queue.
  /**
   * @brief Virtual destructor; any remaining elements are destroyed normally.
   */
  virtual ~ThreadSafeQueue() =
      default;  // Virtual so derived queues clean up correctly; std::queue
                // frees its elements automatically.

  /**
   * @brief Appends a copy of `value` to the back of the queue.
   * @param[in] value Element to copy.
   */
  // Add an element to the queue
  void push(const T& value);  // Overload for lvalues: makes a copy so the
                              // caller's object is unchanged.
  /**
   * @brief Moves `value` onto the back of the queue.
   * @param[in,out] value Element to move from.
   */
  void push(T&& value);  // Overload for rvalues: moves the element in to
                         // avoid an expensive copy.

  /**
   * @brief Removes and returns the front element, if any.
   * @return The front element, or std::nullopt if the queue is empty.
   */
  // Retrieve and remove an element from the front of the queue
  std::optional<T>
  pop();  // Non-blocking take: returns the oldest item or "nothing" if empty.

  /**
   * @brief Reports whether the queue currently has no elements.
   */
  // Check if the queue is empty
  bool   empty() const;  // Returns a snapshot of whether the queue has no
                         // elements right now.

  /**
   * @brief Returns the current number of elements.
   */
  // Get the size of the queue
  size_t size() const;  // Returns a snapshot of the element count.

  /**
   * @brief Discards all elements currently in the queue.
   */
  // Clear the queue
  void   clear();  // Removes every element that is in the queue at the moment
                   // of the call.

  /**
   * @brief Constructs an element in place at the back of the queue.
   * @tparam Args Constructor argument types forwarded to T.
   * @param[in,out] args Arguments forwarded to T's constructor.
   */
  // emplace to the queue
  template <typename... Args>  // A variadic template accepts any number and
                               // types of constructor arguments.
  void             emplace(Args&&... args);  // Builds the element
                                             // directly in the queue,
                                             // avoiding an extra copy
                                             // or move.

  /**
   * @brief Returns a copy of the front element without removing it.
   * @return Copy of the front element, or std::nullopt if the queue is empty.
   */
  // get front element from the queue
  std::optional<T> front();  // Peeks at the oldest item (as a copy) or
                             // returns "nothing" if empty.

 private:
  /**
   * @brief Guards every access to queue_; mutable so const queries can lock
   * it.
   */
  mutable std::mutex mutex_;  // One lock protects the whole queue; "mutable"
                              // lets const methods lock it too.
  /**
   * @brief The underlying FIFO container.
   */
  std::queue<T>      queue_;  // The actual first-in-first-out container holding
                              // the elements.
};

template <typename T>  // Definition of the copy-push overload for the template.
void ThreadSafeQueue<T>::push(
    const T& value)  // Takes a const reference so the source is only read.
{
  // Hold the lock only for the container mutation.
  std::lock_guard<std::mutex> lock(
      mutex_);         // Locks the mutex for this scope and releases it
                       // automatically on exit.
  queue_.push(value);  // Appends a copy of value to the back.
}

template <typename T>  // Definition of the move-push overload.
void ThreadSafeQueue<T>::push(
    T&& value)  // Takes an rvalue reference so the element can be moved from.
{
  std::lock_guard<std::mutex> lock(
      mutex_);  // Lock before touching the shared container.
  queue_.push(std::move(
      value));  // Move the element into the queue instead of copying it.
}

template <typename T>  // Definition of pop.
std::optional<T>
ThreadSafeQueue<T>::pop()  // Returns a value that may be empty.
{
  // unique_lock is used because the mutex is released on every return path.
  std::unique_lock<std::mutex> lock(
      mutex_);  // A flexible lock that can be released on every return path.
  if (queue_.empty())  // Check whether there is anything to take.
  {
    // Empty is not an error: report it without waiting for a producer.
    return std::nullopt;  // Signals "no item" immediately rather than
                          // blocking.
  }
  T value = std::move(queue_.front());  // Move the front element into a local
                                        // so it survives after the pop.
  queue_.pop();  // Remove that now-copied-from element from the queue.
  return value;  // Hands the element back to the caller.
}

template <typename T>  // Definition of empty.
bool ThreadSafeQueue<T>::empty()
    const  // const because inspecting does not change the queue.
{
  std::lock_guard<std::mutex> lock(
      mutex_);            // Lock so the check sees a consistent state.
  return queue_.empty();  // Returns true when there are no elements.
}

template <typename T>  // Definition of size.
size_t ThreadSafeQueue<T>::size()
    const  // const because counting does not change the queue.
{
  std::lock_guard<std::mutex> lock(
      mutex_);           // Lock so the count is taken from a consistent state.
  return queue_.size();  // Returns how many elements are currently stored.
}

template <typename T>             // Definition of clear.
void ThreadSafeQueue<T>::clear()  // Removes all stored elements.
{
  std::lock_guard<std::mutex> lock(
      mutex_);  // Lock for the whole clear operation.
  // Swapping with a fresh queue destroys all elements after the swap.
  std::queue<T> empty;  // A brand-new empty queue used as the swap partner.
  std::swap(queue_,
      empty);  // Exchange contents; when empty goes out of scope it destroys
               // the old elements.
}

template <
    typename T>  // Definition of emplace (outer template for the element type).
template <typename... Args>  // Inner template for the forwarded constructor
                             // arguments.
void ThreadSafeQueue<T>::emplace(Args&&... args)  // Forwards arguments straight
                                                  // to T's constructor.
{
  std::lock_guard<std::mutex> lock(
      mutex_);  // Lock before modifying the container.
  queue_.emplace(std::forward<Args>(
      args)...);  // Construct the element in place, preserving value
                  // categories via std::forward.
}

template <typename T>                         // Definition of front.
std::optional<T> ThreadSafeQueue<T>::front()  // Peeks at the front element.
{
  std::lock_guard<std::mutex> lock(
      mutex_);         // Lock so the peek is safe against concurrent changes.
  if (queue_.empty())  // Check whether an element exists.
  {
    return std::nullopt;  // No element to return: report "nothing" instead
                          // of failing.
  }
  return queue_.front();  // Return a copy of the front element (safe after
                          // the lock is released).
}
