/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file configurator.cpp
 * @brief Explicit Configurator::get<T>() specializations that expose each
 * configuration section.
 * @details The Configurator class declares a generic get<T>() accessor but does
 * not define its behaviour for any particular type. This file supplies one
 * explicit specialization per configuration section. Each one simply returns
 * the matching private member stored in the Configurator object. In plain
 * words: this file is the lookup table that maps a type name, such as
 * ServerConfig, to the actual settings object of that type held by the
 * Configurator.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Code throughout the server asks for its settings by type, for example
 * `store.config().get<Configurator::ServerConfig>()`. Those calls only link
 * successfully because this file defines each specialization. Without it, the
 * compiler accepts the calls but the linker reports undefined references, so
 * the server cannot obtain any of its configuration sections at runtime.
 * Keeping the definitions here (not in the header) also means adding a new
 * section requires updating this single mapping.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - C++ concepts and constraints:
 * https://en.cppreference.com/w/cpp/language/constraints
 * - Explicit (full) template specialization:
 * https://en.cppreference.com/w/cpp/language/template_specialization
 * - Function templates:
 * https://en.cppreference.com/w/cpp/language/function_template
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Brings in the Configurator class whose get<T>() members are defined here.
#include "configurator.hpp"

// Specialization for DOSDetectorConfig
/**
 * @brief Returns the DoS-detection section.
 * @details A DoS (denial-of-service) detector watches request rates to spot
 * abuse. This specialization gives callers access to that section's limits and
 * fingerprint lists.
 * @see Configurator::DOSDetectorConfig
 */
template <>  // This is a full specialization: a hand-written version for
             // exactly one template argument.
Configurator::DOSDetectorConfig&
Configurator::get<Configurator::DOSDetectorConfig>()  // Callers asking for
                                                      // DOSDetectorConfig reach
                                                      // the member below.
{
  return dos_detector_config_;  // Hand back the stored DoS settings section
                                // by reference (no copy).
}

// Specialization for DatabaseConfig
/**
 * @brief Returns the database connection section.
 * @details Exposes host, port, credentials and pool sizing used to connect to
 * PostgreSQL.
 * @see Configurator::DatabaseConfig
 */
template <>  // Full specialization for the database connection settings type.
Configurator::DatabaseConfig&
Configurator::get<Configurator::DatabaseConfig>()  // Maps the DatabaseConfig
                                                   // type to its stored member.
{
  return database_config_;  // Hand back the stored database settings section
                            // by reference.
}

// Specialization for ServerConfig
/**
 * @brief Returns the Drogon server section.
 * @details Exposes identity, networking, threading and logging settings for the
 * HTTP server.
 * @see Configurator::ServerConfig
 */
template <>  // Full specialization for the Drogon server settings type.
Configurator::ServerConfig&
Configurator::get<Configurator::ServerConfig>()  // Maps the ServerConfig type
                                                 // to its stored member.
{
  return server_config_;  // Hand back the stored server settings section by
                          // reference.
}
// Specialization for TokenManagerParameters
/**
 * @brief Returns the token-manager section.
 * @details Exposes how long auth tokens live and the issuer/type/secret used to
 * sign them.
 * @see Configurator::TokenManagerParameters
 */
template <>  // Full specialization for the authentication token settings type.
Configurator::TokenManagerParameters& Configurator::get<
    Configurator::TokenManagerParameters>()  // Maps TokenManagerParameters to
                                             // its stored member.
{
  return token_manager_parameters_;  // Hand back the stored token settings
                                     // section by reference.
}

/**
 * @brief Returns the frontend/CORS section.
 * @details Exposes where the web frontend lives and which origins may call the
 * API (CORS = the browser rule that controls which websites can use the API).
 * @see Configurator::FrontEndConfig
 */
template <>  // Full specialization for the frontend/CORS settings type.
Configurator::FrontEndConfig&
Configurator::get<Configurator::FrontEndConfig>()  // Maps FrontEndConfig to its
                                                   // stored member.
{
  return frontend_config_;  // Hand back the stored frontend settings section
                            // by reference.
}

/**
 * @brief Returns the permission-cache section.
 * @details Exposes how long a cached permission decision stays valid before it
 * is discarded.
 * @see Configurator::PermissionCacheConfig
 */
template <>  // Full specialization for the permission-cache settings type.
Configurator::PermissionCacheConfig& Configurator::get<
    Configurator::PermissionCacheConfig>()  // Maps PermissionCacheConfig to its
                                            // stored member.
{
  return permission_cache_config_;  // Hand back the stored permission-cache
                                    // settings section by reference.
}

/**
 * @brief Returns the password-worker pool section.
 * @details Exposes how many background threads hash passwords and how deep
 * their queue can grow.
 * @see Configurator::PasswordWorkerConfig
 */
template <>  // Full specialization for the password-worker settings type.
Configurator::PasswordWorkerConfig& Configurator::get<
    Configurator::PasswordWorkerConfig>()  // Maps PasswordWorkerConfig to its
                                           // stored member.
{
  return password_worker_config_;  // Hand back the stored password-worker
                                   // settings section by reference.
}

/**
 * @brief Returns the database-executor section.
 * @details Exposes worker count and queue limits for the component that runs
 * database work.
 * @see Configurator::DbExecutorConfig
 */
template <>  // Full specialization for the database-executor settings type.
Configurator::DbExecutorConfig&
Configurator::get<Configurator::DbExecutorConfig>()  // Maps DbExecutorConfig to
                                                     // its stored member.
{
  return db_executor_config_;  // Hand back the stored database-executor
                               // settings section by reference.
}

/**
 * @brief Returns the email-sender daemon section.
 * @details Exposes how to reach the external service that actually delivers
 * emails.
 * @see Configurator::EmailSenderConfig
 */
template <>  // Full specialization for the email-sender settings type.
Configurator::EmailSenderConfig&
Configurator::get<Configurator::EmailSenderConfig>()  // Maps EmailSenderConfig
                                                      // to its stored member.
{
  return email_sender_config_;  // Hand back the stored email-sender settings
                                // section by reference.
}

/**
 * @brief Returns the test-only section.
 * @details Exposes switches that let the test harness start the server without
 * a real database.
 * @see Configurator::Testing
 */
template <>  // Full specialization for the test-only settings type.
Configurator::Testing& Configurator::get<
    Configurator::Testing>()  // Maps the Testing type to its stored member.
{
  return testing_;  // Hand back the stored test-only switch section by
                    // reference.
}
