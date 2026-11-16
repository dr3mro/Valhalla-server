/**
 * @file databaseconnectionpool.cpp
 * @brief Implementation for databaseconnectionpool: Opens one pqxx connection
 * using the configured credentials and a connect timeout.
 * @details This file implements the pool's lifecycle.
 * createDatabaseConnection() opens and validates a single connection; the
 * constructor spins up all slots in parallel with retries;
 * get_connection()/return_connection() lease and return links, measuring waits
 * and throttling saturation warnings; reconnect_all() repairs every idle link
 *          in parallel while holding the pool lock so nothing is handed out
 * half-reconnected.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is where the server decides how many database connections exist and what
 * happens when they are all busy. It translates configuration (DB_MAX_CONN,
 * DB_ACQUIRE_TIMEOUT_MS) into real behaviour, maps a wait that expires to a
 * DatabaseBusy exception (which the HTTP layer answers with 503), and
 * cooperates with the watchdog to recover from database restarts. Without it
 * there would be no connection reuse, no back-pressure, and no automatic
 * recovery.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - std::async and std::future (run work in parallel and collect its result):
 * https://en.cppreference.com/w/cpp/thread/async
 * - std::future::get and wait: https://en.cppreference.com/w/cpp/thread/future
 * - std::atomic::compare_exchange_strong (an atomic "update if unchanged"):
 * https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange
 * - std::chrono::steady_clock (a monotonic clock ideal for timeouts):
 * https://en.cppreference.com/w/cpp/chrono/steady_clock
 * - Exponential backoff (sleep longer after each failed attempt)
 * - exit() and EXIT_FAILURE from the C standard library:
 * https://en.cppreference.com/w/cpp/utility/program/exit
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// The class declaration implemented here.
#include "databaseconnectionpool.hpp"

// fmt::format builds the connection string and all log messages.
#include <fmt/core.h>
#include <fmt/format.h>  // Full formatting header used by the same calls.

#include <algorithm>  // Standard algorithms included for pool utilities.
#include <chrono>     // steady_clock and milliseconds measure acquire waits.
#include <cstdint>  // Fixed-width integer types used by the retry/backoff math.
// Provides exit() and EXIT_FAILURE used on fatal startup failure.
#include <cstdlib>
#include <exception>  // std::exception is the base of all caught errors here.
// std::async and std::future run connection creation/reconnects in parallel.
#include <future>
// std::make_shared and std::shared_ptr create and hold connections.
#include <memory>
#include <mutex>  // Mutex machinery used indirectly by the pool's locking.
// libpqxx connection type opened by createDatabaseConnection().
#include <pqxx/pqxx>
// std::runtime_error thrown when a pool slot cannot be created.
#include <stdexcept>
// std::string builds the connection string and holds error text.
#include <string>
#include <thread>  // std::this_thread::sleep_for implements the retry backoff.
// std::move hands connections and futures into containers without copying.
#include <utility>
#include <vector>  // std::vector collects the futures for all parallel tasks.

// Configurator::DatabaseConfig and ServerConfig supply the settings.
#include "configurator/configurator.hpp"
#include "database.hpp"  // The Database wrapper created for each pool slot.
// Same header via a different include path used by this build.
#include "database/database.hpp"
// DatabaseBusy is thrown when the acquire timeout expires.
#include "database/databasebusy.hpp"
// Store::getObject<> locates the shared Configurator at construction.
#include "store/store.hpp"
// Message helpers emit the startup, warning and fatal logs.
#include "utils/message/message.hpp"

/**
 * Opens one pqxx connection using the configured credentials and a connect
 * timeout. Returns a Database only when the connection is open; failures are
 * logged rather than thrown so the caller can retry the whole slot. config_:
 * Configuration holding host, dbname, user and password. Returns A validated
 * Database, or `nullptr` on failure.
 */
std::shared_ptr<Database> DatabaseConnectionPool::createDatabaseConnection(
    const auto& config_)
{
  try  // libpqxx throws on bad credentials or an unreachable server; the
       // caller prefers a nullptr to an exception.
  {
    auto conn = std::make_shared<pqxx::connection>(  // Build a connection from
                                                     // a libpq-style
                                                     // keyword/value string.
        fmt::format("host={} dbname={} user={} password={} connect_timeout={}",
                    config_.host,
                    config_.name,
                    config_.user,
                    config_.pass,
                    TIMEOUT)
            .c_str());  // connect_timeout stops a dead server from blocking
                        // a worker forever.

    if (conn->is_open())  // Only a successfully opened socket is worth
                          // wrapping.
    {
      return std::make_shared<Database>(
          std::move(conn));  // Hand the connection to Database, which
                             // validates it with SELECT 1.
    }

    Message::CriticalMessage(
        "Failed to open database connection.");  // Report a connection
                                                 // object that was created
                                                 // but not open.
  }
  catch (const std::exception&
             e)  // Turn any driver exception into a log entry plus nullptr.
  {
    Message::CriticalMessage(
        fmt::format("Failed to create a Database connection: {}",
                    e.what()));  // Include the driver's reason.
  }
  return nullptr;  // Signal failure so the caller can retry this slot.
}

/**
 * @brief Initialises the pool from the Configurator; failure terminates the
 * process.
 * @details Reads DB_MAX_CONN and the acquire timeout, warns when the pool is
 * undersized, then starts all connections concurrently. Each slot retries with
 * exponential backoff (MAX_RETRIES attempts); if any slot cannot be created, a
 * runtime_error is raised and the catch block exits the process with
 * EXIT_FAILURE.
 */
DatabaseConnectionPool::DatabaseConnectionPool()
    : configurator_(
          Store::getObject<Configurator>())  // Resolve the shared configuration
                                             // object before the body runs.
{
  try  // Any setup failure is fatal, so it is funnelled into the exit path
       // below.
  {
    const Configurator::DatabaseConfig& config = configurator_->get<
        Configurator::
            DatabaseConfig>();  // Read the
                                // DB_MAX_CONN/acquire-timeout/credential
                                // settings once.
    acquireTimeout_ = std::chrono::milliseconds(
        config.acquire_timeout_ms);  // Cache the timeout so get_connection()
                                     // need not touch configuration later.

    // Every server thread blocks on one connection while it queries, and
    // the watchdog needs one more. A smaller pool makes requests queue (and
    // time out with 503) even though nothing is slow.
    const unsigned int server_threads =
        configurator_->get<Configurator::ServerConfig>()
            .threads;  // Number of Drogon IO threads that may query at
                       // once.
    if (config.max_conn <
        server_threads + 1U)  // Need one slot per thread plus one spare for
                              // the watchdog's health checks.
    {
      Message::WarningMessage(
          fmt::format("DB_MAX_CONN ({}) is below SERVER_THREADS + 1 ({}): "
                      "requests "
                      "may queue for a connection.",
                      config.max_conn,
                      server_threads +
                          1U));  // Warn early so an operator can raise
                                 // the pool size before users see 503s.
    }

    std::vector<std::future<std::shared_ptr<Database>>>
        futures;  // One future per connection slot, used to collect the
                  // parallel results.

    futures.reserve(config.max_conn);  // Reserve once so push_back does not
                                       // reallocate while tasks are launched.
    for (uint16_t i = 0; i < config.max_conn;
         ++i)  // One iteration per configured pool slot.
    {
      futures.push_back(
          std::async(std::launch::async,  // Start each connection attempt on
                                          // its own thread so all slots open in
                                          // parallel.
                     [this, config]()
                         -> std::shared_ptr<Database>  // Capture the pool and
                                                       // config by value; the
                                                       // task outlives this
                                                       // loop iteration.
                     {
                       unsigned int retryCount =
                           0;  // Counts failed attempts for the
                               // backoff and the warning message.
                       while (retryCount <
                              MAX_RETRIES)  // Stop after the configured
                                            // number of attempts.
                       {
                         auto conn = createDatabaseConnection(
                             config);  // Try once to open and
                                       // validate a connection.
                         if (conn)     // A non-null pointer means the slot is
                                       // ready.
                         {
                           return conn;  // Hand the finished connection back
                                         // to the constructor.
                         }
                         std::this_thread::sleep_for(std::chrono::seconds(
                             1U << retryCount));  // Exponential backoff: 1s,
                                                  // 2s, 4s... gives the DB time
                                                  // to become ready.
                         Message::WarningMessage(
                             fmt::format("Failed to create a database "
                                         "connection. Attempt "
                                         "{}/{}...",
                                         ++retryCount,
                                         MAX_RETRIES));  // Log the attempt
                                                         // number, then loop.
                       }
                       return nullptr;  // All attempts failed for this slot.
                     }));
    }

    // Wait for all futures
    for (auto& future : futures)  // Walk the collected results in slot order.
    {
      auto conn =
          future.get();     // Blocks only on completion of the individual task
      if (conn != nullptr)  // This slot was created successfully.
      {
        pool_.release(conn);  // Put the new connection into the idle
                              // set so it can be leased at once.
        Message::InitMessage(
            fmt::format("Connection {}/{} created successfully.",
                        pool_.idle(),
                        config.max_conn));  // Report progress using the number
                                            // now idle.
      }
      else
      {
        Message::ErrorMessage(
            "Failed to establish one or more connections after maximum "
            "retries.");  // One bad slot is enough to abort startup.
        throw std::runtime_error(
            // Jump to the catch below, which exits the process.
            "Database connection pool initialization failure.");
      }
    }
  }
  catch (const std::exception&
             e)  // Catch the runtime_error above (or any other setup failure).
  {
    std::string what = e.what();  // Copy the error text so it can be
                                  // formatted into the log messages.
    Message::CriticalMessage(
        fmt::format("Failed to initialize database connection pool: {}",
                    what));  // Log the detailed reason.
    Message::ErrorMessage(
        "Fatal Error!\nExiting...");  // Tell the operator this is not
                                      // recoverable.
    exit(EXIT_FAILURE);
    /*NOLINT*/  // Terminate: a partially built pool would serve broken
                // requests.
  }
}

/**
 * @brief Leases an idle connection, waiting at most the configured acquire
 * timeout.
 * @return A `shared_ptr` to be returned with return_connection() when work is
 * done.
 * @throws DatabaseBusy If no connection became free before the timeout expired.
 * @details Measures the wait with a steady clock and emits at most one
 * saturation warning per second using an atomic compare-exchange on the
 * last-warning timestamp.
 */
std::shared_ptr<Database> DatabaseConnectionPool::get_connection()
{
  const auto timeout =
      acquireTimeout_;  // Snapshot the timeout read at startup; it never
                        // changes at runtime.
  const auto started =
      std::chrono::steady_clock::now();  // A monotonic start time for
                                         // measuring the wait.

  std::shared_ptr<Database> db_ptr = pool_.acquire(
      timeout);  // Block until a connection is free or the timeout elapses.

  const auto waited_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();  // Convert the elapsed time to
                                        // milliseconds for logging/comparison.

  if (db_ptr == nullptr)  // No connection became free in time.
  {
    Message::CriticalMessage(
        fmt::format("Timeout ({} ms) while waiting for a database connection",
                    timeout.count()));  // Log how long callers waited.
    throw DatabaseBusy();  // The HTTP layer maps this exception to a
                           // retryable 503.
  }

  if (waited_ms >= SLOW_WAIT_WARNING_MS)  // Anything slower than the threshold
                                          // suggests pool saturation.
  {
    // At most one warning per second, so a saturated pool does not flood
    // the log.
    const std::int64_t now_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();  // A stable wall-independent timestamp for rate
                       // limiting.
    std::int64_t last = lastSlowWaitWarningMs_
                            .load();  // Read the last warning time atomically.
    if (now_ms - last >= 1000 &&
        lastSlowWaitWarningMs_
            .compare_exchange_strong(last,
                                     now_ms))  // Only the thread that
                                               // successfully updates the
                                               // timestamp logs, at most once
                                               // per second.
    {
      Message::WarningMessage(
          fmt::format("Waited {} ms for a database connection: the pool is "
                      "saturated "
                      "(see DB_MAX_CONN).",
                      waited_ms));  // Point operators at the setting that
                                    // likely needs raising.
    }
  }

  return db_ptr;  // Give the caller the leased connection; they must return
                  // it later.
}

/**
 * Returns a leased connection to the idle set, waking one waiting acquirer.
 * db_ptr: Connection to return; null values are ignored by BlockingPool.
 */
void DatabaseConnectionPool::return_connection(
    std::shared_ptr<Database>&& db_ptr)
{
  pool_.release(std::move(db_ptr));
}  // Moving avoids a refcount bump and puts the link back in the idle deque.

/**
 * @brief Reconnects all currently idle connections in parallel.
 * @details Uses forEachIdle to hold the pool lock while the reconnect tasks run
 * and until every task has been waited on, so no idle connection is handed out
 * half-way through a reconnect. Busy (already leased) connections are not
 * touched here.
 * @note Each reconnect's success or failure (including exceptions) is logged;
 * the method itself does not throw for an individual connection failure.
 */
void DatabaseConnectionPool::reconnect_all()
{
  const Configurator::DatabaseConfig& config = configurator_->get<
      Configurator::DatabaseConfig>();  // Read the configured pool size to
                                        // reserve the future vector.

  std::vector<std::future<void>>
      futures;  // One future per idle connection being reconnected.
  futures.reserve(config.max_conn);  // Avoid reallocations while launching
                                     // the reconnect tasks.

  // The pool lock is held until every reconnect finished, so no idle
  // connection can be handed out (and used) while it is being reconnected.
  pool_.forEachIdle(  // Holds the pool lock across both callbacks: nothing
                      // idle is leased during the repair.
      [&futures](std::shared_ptr<Database>&
                     connection)  // Called once for each idle connection still
                                  // in the pool.
      {
        futures.push_back(std::async(
            std::launch::async,  // Start each reconnect on its own thread
                                 // so all idle links are refreshed
                                 // together.
            [&connection]()
            {
              try  // A failed reconnect of one link must not abort the
                   // others.
              {
                if (connection->reconnect())  // Ask the Database to
                                              // rebuild its link from
                                              // the cached string.
                {
                  Message::InfoMessage(
                      fmt::format("Database connection {} is re-established "
                                  "successfully",
                                  static_cast<void*>(
                                      connection
                                          .get())));  // Log success with
                                                      // the object address
                                                      // for tracing.
                }
                else
                {
                  Message::ErrorMessage(
                      fmt::format("Database connection {} failed to be "
                                  "re-established",
                                  static_cast<void*>(
                                      connection
                                          .get())));  // Log a non-throwing
                                                      // reconnect failure.
                }
              }
              catch (
                  const std::exception& e)  // Catch unexpected driver
                                            // exceptions so the loop continues.
              {
                Message::CriticalMessage(
                    fmt::format("Reconnect exception: {}",
                                e.what()));  // Record the exception text for
                                             // diagnosis.
              }
            }));
      },
      [&futures]()
      {
        for (auto& future : futures)  // Visit every launched reconnect task.
        {
          future.wait();  // Block until it finishes, still under the pool
                          // lock, before any link can be leased.
        }
      });
}
