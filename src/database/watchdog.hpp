/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file watchdog.hpp
 * @brief Background health monitor for the pooled database connections.
 * @details This header declares WatchDog, a small class that owns a background
 * thread. The thread wakes once per interval, leases one pooled connection
 * through a DatabaseHanndler, checks whether the link is alive, and reconnects
 * it (and the rest of the idle pool) when it is not. An atomic flag lets the
 * destructor signal shutdown and then join the thread cleanly.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Databases restart and network links drop; without monitoring, a stalled
 * connection would keep failing requests until somebody manually restarted the
 * server. The watchdog runs in the background at startup and silently repairs
 * connections, cooperating with DatabaseConnectionPool::reconnect_all(). It is
 * the self-healing part of the synchronous database path: controllers do not
 * need to know that a link died and came back.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - std::thread (a real background thread of execution):
 * https://en.cppreference.com/w/cpp/thread/thread
 * - std::atomic (a flag readable/writable from two threads without races):
 * https://en.cppreference.com/w/cpp/atomic/atomic
 * - join() (wait for a thread to finish before destroying its owner):
 * https://en.cppreference.com/w/cpp/thread/thread/join
 * - RAII (the destructor stops and joins the thread):
 * https://en.cppreference.com/w/cpp/language/raii
 * - std::chrono::seconds (a time duration):
 * https://en.cppreference.com/w/cpp/chrono/duration
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include guard: define this class at most once per translation
              // unit.

// std::atomic<bool> is the shutdown flag shared with the monitor thread.
#include <atomic>
#include <chrono>  // std::chrono::seconds holds the check interval.
// libpqxx declarations used indirectly by the connection checks the thread
// performs.
#include <pqxx/pqxx>
#include <thread>  // std::thread is the monitor thread itself.

/**
 * @class WatchDog
 * @brief Background health monitor for the pooled database connections.
 * @details On construction it starts a background work thread that periodically
 *          leases one connection, checks it, and reconnects it if the link is
 * lost (also triggering a reconnect of all idle connections through the pool).
 * The monitor flag is atomic so the destructor can signal shutdown and then
 * join the thread. Non-copyable and non-movable.
 * @note The destructor may block for up to one check interval while the thread
 * wakes up.
 * @see DatabaseHanndler, DatabaseConnectionPool::reconnect_all
 */
class WatchDog  // Owns one long-lived thread that keeps checking and repairing
                // database links.
{
 public:
  /**
   * @brief Starts the monitoring thread.
   * @throws std::logic_error If a monitor thread appears already joinable (a
   * fresh object has none, so this guard is effectively inactive on
   * construction).
   */
  WatchDog();  // Sets up the loop and launches the background thread before
               // returning.
  WatchDog(const WatchDog&) =
      delete;  // Copying would create two owners of the same thread.
  WatchDog(WatchDog&&) = delete;  // Moving a live thread's owner is unsafe.
  WatchDog& operator=(const WatchDog&) =
      delete;  // Copy-assignment would duplicate thread ownership.
  WatchDog& operator=(WatchDog&&) =
      delete;  // Move-assignment would duplicate thread ownership.

  /**
   * @brief Signals the monitor thread to stop and joins it before returning.
   */
  virtual ~WatchDog();  // Clears the atomic flag and waits for the thread, so
                        // it never touches a destroyed object.

 private:
  /**
   * Atomic stop flag read by the monitor loop and set by the destructor.
   */
  std::atomic<bool> should_monitor{
      true};  // Starts true; the loop reads it each iteration and the
              // destructor makes it false.

  /**
   * Sleep between health checks (also bounds destructor join latency).
   */
  std::chrono::seconds check_interval{
      1};  // One second keeps checks frequent without hammering the database.

  /**
   * The thread running the monitoring loop; joined by the destructor.
   */
  std::thread monitor_thread;  // The actual background worker; joinable after
                               // construction.
};
