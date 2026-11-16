/**
 * @file watchdog.cpp
 * @brief Implementation for watchdog: Starts the monitoring thread that
 * periodically checks a pooled connection.
 * @details This file implements WatchDog. The constructor guards against a
 * pre-existing thread and then launches a lambda that loops while the atomic
 * flag is set. Each cycle leases a connection with an RAII handler, checks it,
 * and on failure reconnects it and asks the pool to reconnect all idle links.
 * DatabaseBusy and other exceptions are caught and followed by an interval
 * sleep. The destructor clears the flag and joins the thread before the
 * watchdog object disappears.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It is the server's automatic recovery mechanism for the synchronous database
 * path. A database restart or a dropped socket is detected here within one
 * second and repaired without any request failing permanently. Because the
 * monitor leases its connection through a DatabaseHanndler, it also
 * participates in the same pool and back-pressure rules as regular requests.
 * Without this file, stale connections would keep failing until manual
 * intervention.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - std::thread and lambdas as thread entry points:
 * https://en.cppreference.com/w/cpp/thread/thread
 * - std::atomic (the stop flag shared between threads):
 * https://en.cppreference.com/w/cpp/atomic/atomic
 * - RAII with std::unique_ptr (automatic cleanup):
 * https://en.cppreference.com/w/cpp/memory/unique_ptr
 * - Exceptions and catch blocks:
 * https://en.cppreference.com/w/cpp/language/try_catch
 * - std::this_thread::sleep_for (pause a thread):
 * https://en.cppreference.com/w/cpp/thread/sleep_for
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#include "database/watchdog.hpp"  // The WatchDog declaration implemented here.

#include <fmt/core.h>    // fmt::format builds the descriptive log messages.
#include <fmt/format.h>  // Full formatting header used by the same calls.

// std::exception, the base type of the generic catch blocks.
#include <exception>
// std::unique_ptr and std::make_shared create the RAII handler.
#include <memory>
#include <stdexcept>  // std::logic_error thrown by the constructor's guard.
// std::thread and std::this_thread::sleep_for drive the monitor loop.
#include <thread>

// Database::check_connection()/reconnect() are called on the leased link.
#include "database/database.hpp"
// DatabaseBusy is caught separately because a full pool is expected, not a
// fault.
#include "database/databasebusy.hpp"
// DatabaseHanndler provides the RAII lease used each cycle.
#include "database/databasehandler.hpp"
#include "utils/message/message.hpp"  // Message helpers log every state change.

/**
 * @brief Starts the monitoring thread that periodically checks a pooled
 * connection.
 * @details The loop leases a connection through a DatabaseHanndler (so it is
 * returned on scope exit), checks it, and on failure reconnects it and then
 * asks the pool to reconnect all idle connections. DatabaseBusy, a lost
 * connection and any other exception are logged (or warned) and followed by an
 * interval sleep, so one bad cycle cannot spin the loop. The thread reads the
 * atomic stop flag each iteration.
 * @throws std::logic_error If a monitor thread is already joinable.
 */
WatchDog::WatchDog()
{
  if (monitor_thread.joinable())  // A fresh object has no thread; this guard
                                  // prevents accidentally replacing a live one.
  {
    throw std::logic_error(
        "WatchDog thread already running!");  // Fail loudly rather than
                                              // lose track of a running
                                              // thread.
  }

  // Start the monitoring thread
  monitor_thread = std::thread(  // Launch the loop on its own thread; it runs
                                 // until should_monitor becomes false.
      [this]()  // Capture `this` so the loop can read the flag,
                // interval and call members; the destructor joins
                // before `this` dies.
      {
        while (should_monitor)  // The atomic stop flag is checked
                                // every cycle for a prompt,
                                // race-free shutdown.
        {
          try  // Keep one bad iteration from killing the whole
               // monitoring thread.
          {
            // RAII lease: the connection is returned to the
            // pool when the handler dies.
            std::unique_ptr<DatabaseHanndler> dbHandler =
                std::make_unique<DatabaseHanndler>();  // Heap allocation
                                                       // keeps the lease
                                                       // alive for the whole
                                                       // iteration and
                                                       // returns it
                                                       // automatically.
            std::shared_ptr<Database> db_ptr =
                dbHandler->get_connection();  // Reach the actual
                                              // connection to check.

            if (db_ptr == nullptr)  // Defensive: the handler normally
                                    // throws instead of returning null.
            {
              Message::WarningMessage(
                  "WatchDog could not acquire a database "
                  "connection "
                  "within timeout.");  // Warn that this cycle
                                       // could not run.
              std::this_thread::sleep_for(
                  check_interval);  // Wait before retrying so
                                    // the loop does not spin.
              continue;             // Skip the rest of this cycle and
                                    // check the stop flag again.
            }

            if (!db_ptr->check_connection())  // A failed SELECT 1
                                              // probe means the
                                              // link is dead.
            {
              Message::WarningMessage(
                  "Database connection lost. Attempting to "
                  "reconnect...");  // Human-readable notice
                                    // of the repair attempt.
              Message::CriticalMessage(
                  fmt::format("Database connection {} link is lost.",
                              static_cast<const void*>(
                                  db_ptr.get())));  // Include the
                                                    // connection address
                                                    // for tracing.
              try  // Reconnecting can throw, so isolate it
                   // from the outer loop handler.
              {
                if (db_ptr->reconnect())  // Rebuild this
                                          // connection from
                                          // its cached
                                          // credentials.
                {
                  Message::InfoMessage(
                      fmt::format("Database connection "
                                  "{} is re-established "
                                  "successfully",
                                  static_cast<void*>(
                                      // Log the repaired link. Also
                                      // refresh the rest of the idle pool
                                      // after this link recovered.
                                      db_ptr.get())));
                  // Ask the pool to refresh all other idle
                  // connections too.
                  dbHandler->reconnect_all();
                }
                else
                {
                  Message::ErrorMessage(
                      "Failed to reconnect to "
                      "database");  // Report a
                                    // non-throwing
                                    // reconnect
                                    // failure.
                }
              }
              catch (const std::exception& e)  // A driver exception
                                               // during reconnect should
                                               // not stop monitoring.
              {
                Message::CriticalMessage(
                    fmt::format("Reconnect exception: {}",
                                e.what()));  // Record the exception
                                             // text.
              }
            }

            std::this_thread::sleep_for(
                check_interval);  // Pace the loop so health
                                  // checks happen once per
                                  // interval.
          }
          // A saturated pool is not a fault: log and try again
          // after the interval.
          catch (const DatabaseBusy&)  // Expected under load: all
                                       // connections were leased
                                       // when the watchdog tried.
          {
            Message::WarningMessage(
                "WatchDog could not acquire a database "
                "connection "
                "within timeout.");  // Note the skipped check
                                     // without treating it as a
                                     // crash.
            std::this_thread::sleep_for(check_interval);  // Back off before the
                                                          // next attempt.
          }
          catch (const std::exception& e)  // Any other error is logged
                                           // but never ends the thread.
          {
            Message::CriticalMessage(
                fmt::format("Monitoring exception: {}",
                            e.what()));  // Include the exception text
                                         // for diagnosis.
            std::this_thread::sleep_for(
                check_interval);  // Sleep so a persistent error
                                  // cannot hot-loop.
          }
        }
      });
}

/**
 * @brief Stops the monitor and waits for its thread to finish.
 * @details The atomic flag is cleared first so the loop exits at its next
 * iteration; join then guarantees the thread is no longer using `this` before
 * destruction. This must happen before the pool the thread borrows from is torn
 * down.
 */
WatchDog::~WatchDog()
{
  // Ensure proper cleanup
  should_monitor = false;         // Signal the loop to stop at its next check
                                  // (atomic, so no data race).
  if (monitor_thread.joinable())  // Only join a thread that is still running.
  {
    monitor_thread.join();  // Wait for it to finish so it cannot touch
                            // `this` after destruction.
  }
}
