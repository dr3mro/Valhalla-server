/**
 * @file passwordworker.cpp
 * @brief Builds the password thread pool using settings from the Configurator.
 *        (Originally: "Implementation for passwordworker: Fetches the
 * password-worker sizing from the globally configured Configurator.")
 * @details This file reads how many worker threads and how large a queue to
 *          create, then passes those numbers to the BoundedExecutor base class.
 *          It contains only two constructors, no logic of its own.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It connects the password worker pool to the server's runtime configuration,
 * so administrators can tune how much password work the server will do at once
 * without changing code. Without it, the pool sizes would be hard-coded.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Constructor initializer lists:
 * https://en.cppreference.com/w/cpp/language/initializer_list
 * - The singleton/service-locator pattern:
 * https://en.wikipedia.org/wiki/Service_locator_pattern
 * - Configuration objects: https://en.wikipedia.org/wiki/Configuration_file
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// The declarations implemented here.
#include "gatekeeper/passwordcrypt/passwordworker.hpp"

#include <cstddef>  // std::size_t for the constructor parameters.

// Configurator::PasswordWorkerConfig, the settings we read.
#include "configurator/configurator.hpp"
// Store, the registry that holds the global Configurator object.
#include "store/store.hpp"

// An unnamed namespace keeps these helpers private to this .cpp
// file.
namespace {
  /** @brief Fetches the password-worker sizing from the globally configured
   * Configurator. */
  const Configurator::PasswordWorkerConfig& config() {
    return Store::getObject<Configurator>()
        ->get<Configurator::PasswordWorkerConfig>();
  }  // Looks up the shared config and returns its password-worker section by
     // reference.
}  // namespace

/** @brief Sizes the pool from the Configurator; see
 * PasswordWorker::PasswordWorker(). */
PasswordWorker::PasswordWorker()
    : BoundedExecutor(config().workers, config().queue_max, "Password worker") {
}  // Forwards configured worker count, queue limit and a log label to the base
   // class.

/** @brief Sizes the pool explicitly; see
 * PasswordWorker::PasswordWorker(std::size_t, std::size_t). */
PasswordWorker::PasswordWorker(std::size_t workers, std::size_t queue_max)
    : BoundedExecutor(workers, queue_max, "Password worker") {
}  // Forwards caller-supplied sizes to the base class; handy for tests.
