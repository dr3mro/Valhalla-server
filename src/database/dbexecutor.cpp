/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file dbexecutor.cpp
 * @brief Implementation for dbexecutor: Builds the executor from configuration,
 * naming it "DB executor" for diagnostics.
 * @details This file implements the two DbExecutor constructors. Both forward
 * the worker, queue and name settings to the BoundedExecutor base and store the
 * IO fast-path flag. The default constructor reads every setting from the
 * application-wide Configurator; the other takes explicit values, which keeps
 * unit tests independent of configuration.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It is the small piece that turns configured numbers into the actual worker
 * pool used for all blocking database work. The "DB executor" label passed to
 * the base class shows up in logs and statistics, so operators can tune worker
 * counts and queue sizes. Without these constructors the DbExecutor declared in
 * the header could not be created, and routes would have nowhere to send
 * blocking queries.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Base-class initialisation in a constructor initializer list:
 * https://en.cppreference.com/w/cpp/language/initializer_list
 * - Anonymous namespaces (keep helpers private to one .cpp file):
 * https://en.cppreference.com/w/cpp/language/namespace
 * - Configuration via a service locator / Store pattern
 * - std::size_t: https://en.cppreference.com/w/cpp/types/size_t
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// The DbExecutor declaration implemented here.
#include "database/dbexecutor.hpp"

// std::size_t parameters used by the explicit-size constructor.
#include <cstddef>

// Configurator::DbExecutorConfig holds workers, queue_max and io_fast_path.
#include "configurator/configurator.hpp"
// Store::getObject<> locates the shared Configurator.
#include "store/store.hpp"

namespace  // An anonymous namespace makes the helper below visible only inside
           // this .cpp file.
{
  /**
   * Returns the DB executor settings held by the application-wide
   * Configurator.
   */
  const Configurator::DbExecutorConfig& config()
  {
    return Store::getObject<Configurator>()
        ->get<Configurator::DbExecutorConfig>();
  }  // Fetching fresh each time means construction always sees the active
     // settings.
}  // namespace

/**
 * @brief Builds the executor from configuration, naming it "DB executor" for
 * diagnostics.
 */
DbExecutor::DbExecutor()
    : BoundedExecutor(config().workers, config().queue_max, "DB executor"),
      io_fast_path_(config().io_fast_path)
{
}  // The initializer list forwards sizes to the base and caches the fast-path
   // flag.

/**
 * Builds the executor with explicit sizes, naming it "DB executor" for
 * diagnostics. workers: Number of worker threads (zero selects inline mode).
 * queue_max: Maximum queued tasks across all workers.
 * io_fast_path: Whether the IO-thread probe path is enabled.
 */
DbExecutor::DbExecutor(
    std::size_t workers, std::size_t queue_max, bool io_fast_path)
    : BoundedExecutor(workers, queue_max, "DB executor"),
      io_fast_path_(io_fast_path)
{
}  // Used by tests that want deterministic sizes without touching global
   // configuration.
