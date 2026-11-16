/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file defaults.hpp
 * @brief Default values for every environment-driven configuration option.
 * @details This header declares a set of compile-time constants, grouped by
 * purpose into small namespaces. They are the "fallback" values the
 * configuration system uses whenever an environment variable is not set or
 * cannot be understood. Because they are baked into the binary, they guarantee
 * the server still has sensible, complete settings even on a fresh machine with
 * no configuration at all. A few values are deliberately insecure development
 * placeholders and are meant to be replaced in a real deployment.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the single source of truth for "what the server does when nobody
 * tells it what to do". Every Configurator section constructor passes one of
 * these constants to EnvLoader as the default, so changing a number here
 * changes the out-of-the-box behaviour everywhere. It also documents, in one
 * place, the expected units and ranges for each setting (seconds, milliseconds,
 * ports). Without it, a missing environment variable would leave settings
 * undefined and startup would fail.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Environment variables: https://man7.org/linux/man-pages/man7/environ.7.html
 * - Fixed-width integer types such as uint32_t:
 * https://en.cppreference.com/w/cpp/types/integer
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this file at most once per translation unit, no matter
              // how many times it is included.

// Provides fixed-width integer types (uint16_t, uint32_t, uint64_t) with
// predictable sizes.
#include <cstdint>
// std::string holds textual defaults such as host names and credentials.
#include <string>
/** @brief Build-time git tag of the project; used as the default server
 * version. */
extern const char* const
    VALHALLA_GIT_TAG;  // Defined in the build system; lets the reported version
                       // track the exact commit that was built.
/**
 * @namespace Defaults
 * @brief Compile-time fallback values used when a configuration environment
 * variable is absent or cannot be parsed.
 *
 * @details Every nested namespace groups the defaults for one Configurator
 * section. These constants are baked into the binary, not read from a file;
 * deployment overrides come from the environment via EnvLoader. Several entries
 * are deliberately insecure development placeholders (database password, token
 * secret, wildcard CORS) and must be overridden in production.
 * @see Configurator, EnvLoader
 */
namespace Defaults  // Groups all fallback constants so callers write
                    // Defaults::Server::SERVER_PORT_, etc.
{
  /**
   * @namespace Defaults::DosDetector
   * @brief Default thresholds and durations for the DoS detector.
   */
  namespace DosDetector  // DoS = denial of service; these values decide when
                         // a client is sending too many requests.
  {

    /**
     * Default values for DOSDetector configuration.
     */
    /** @brief Maximum requests per @ref PERIOD_ window. */
    const uint32_t MAX_REQUESTS_ =
        10000;  // 32-bit unsigned count is ample and cannot be negative;
                // raised if the env var MAX_REQUESTS is set.
    /** @brief Length (seconds) of the request counting window. */
    const uint32_t PERIOD_ = 30;  // How many seconds of history are counted
                                  // toward MAX_REQUESTS_.
    /** @brief Maximum distinct client fingerprints retained. */
    const uint32_t MAX_FPS_ = 100;  // FPS = fingerprints; caps memory used
                                    // to track unique clients.
    /** @brief Seconds a client stays rate-limited. */
    const uint32_t RL_DURATION_ =
        30;  // How long a client must wait after exceeding the rate limit.
    /** @brief Seconds a banned client stays blocked. */
    const uint32_t BAN_DURATION_ = 3600;  // An hour, in seconds; used for more
                                          // serious abusers than rate limiting.
    /** @brief Seconds between sweeps of expired limit/ban entries. */
    const uint32_t CLN_FRQ_ = 30;  // CLN_FRQ = cleanup frequency; how often old
                                   // entries are removed to free memory.
  };  // namespace DosDetector

  /**
   * @namespace Defaults::Database
   * @brief Default PostgreSQL connection settings.
   */
  namespace Database  // Settings used when opening connections to the
                      // PostgreSQL database.
  {
    /**
     * Default values for Database configuration.
     */
    /** @brief SSL is off by default for local development. */
    const bool DB_SSL_ = false;  // bool because SSL is simply on/off;
                                 // enable it for production traffic.
    // Asynchronous database access on the IO loops (Drogon DbClient); false
    // keeps the synchronous pool path.
    const bool DB_ASYNC_ = true;  // true means queries run without blocking
                                  // the server's IO threads.
    /** @brief Pool size; zero (when set by an env var) triggers CPU-based
     * auto-sizing. */
    const uint8_t DB_MAX_CONN_ =
        10;  // Small uint8_t is enough for a connection
             // count; 10 connections by default.
    // How long a request waits for a free connection before getting 503
    // (milliseconds).
    const uint32_t DB_ACQUIRE_TIMEOUT_MS_ =
        3000;  // 3 seconds expressed in milliseconds; prevents requests
               // hanging forever when the pool is busy.
    /** @brief Default PostgreSQL port. */
    const uint16_t DB_PORT_ =
        5432;  // PostgreSQL's standard port; 16-bit unsigned
               // is the natural type for a port number.
    /** @brief Default listen address (all interfaces). */
    const std::string DB_HOST_ =
        "0.0.0.0";  // Text host address; 0.0.0.0 means "accept connections
                    // on every network interface".
    /** @brief Default database name. */
    const std::string DB_NAME_ =
        "postgres";  // The default database that ships with
                     // PostgreSQL; overridden via DB_NAME.
    /** @brief Default database user. */
    const std::string DB_USER_ =
        "postgres";  // The default superuser name; overridden via DB_USER.
    /** @brief Insecure development credential; override DB_PASS in real
     * deployments. */
    const std::string DB_PASS_ =
        "postgres";  // Placeholder password only for local development;
                     // never use this in production.

  };  // namespace Database

  /**
   * @namespace Defaults::Server
   * @brief Default Drogon server identity, networking, logging and upload
   * settings.
   */
  namespace Server  // Identity and behaviour of the HTTP server itself.
  {
    /**
     * Default values for Server configuration.
     */
    /** @brief Default number of IO threads. */
    const uint16_t SERVER_THREADS_ =
        4;  // Threads that handle network events; a small unsigned count
            // suits a thread count.
    /** @brief Default HTTP port. */
    const uint16_t SERVER_PORT_ =
        8080;  // Common alternative HTTP port, chosen to avoid needing root
               // privileges like port 80.
    /** @brief Default advertised server name. */
    const std::string SERVER_NAME_ =
        "ProjectValhalla";  // Name reported about the server; text because
                            // it is a label, not a number.
    /** @brief Default version, taken from the build-time git tag. */
    const std::string SERVER_VER_ = std::string(
        VALHALLA_GIT_TAG);  // Converts the C-string build tag to a C++
                            // string so it can be logged/formatted.
    /** @brief Default server description. */
    const std::string SERVER_DESC_ =
        "ProjectValhalla API";  // Short human-readable description of what
                                // the server provides.
    /** @brief Default listen address (all interfaces). */
    const std::string SERVER_HOST_ =
        "0.0.0.0";  // Bind to every interface so the server is reachable
                    // from outside the container.
    /** @brief Default log verbosity (1 == info). */
    const uint8_t SERVER_DEBUG_LEVEL_ =
        1;  // A small integer level: 0 debug, 1
            // info, 2 warning, 3 error, 4 critical.
    /** @brief Log to console by default. */
    const bool SERVER_LOG_TO_CONSOLE_ =
        true;  // true so developers see messages in
               // the terminal while developing.
    /** @brief Log to file by default. */
    const bool SERVER_LOG_TO_FILE_ = true;  // true so messages are also kept on
                                            // disk for later inspection.
    /** @brief Default log directory. */
    const std::string SERVER_LOG_DIR_ =
        "./logs/";  // Relative path to a folder inside the working
                    // directory for log files.
    /** @brief Default log file name. */
    const std::string SERVER_LOG_FILE_ =
        "server.log";  // Name of the file written inside SERVER_LOG_DIR_.
    /** @brief Default upload directory. */
    const std::string SERVER_UPLOAD_DIR_ =
        "./uploads/";  // Folder where files sent by clients are stored.

  }  // namespace Server

  /**
   * @namespace Defaults::TokenParameters
   * @brief Default auth-token parameters.
   * @warning TOKEN_SECRET_ is a public development placeholder and must be
   * overridden.
   */
  namespace TokenParameters  // Settings for issuing and verifying
                             // authentication tokens.
  {
    const uint64_t TOKEN_VALIDITY_ =
        43200;  // minutes: 43200 = 30 days. 64-bit because the value is
                // large; overridden with TOKEN_VALIDITY.
    /** @brief Default issuer claim. */
    const std::string TOKEN_ISSUER_ =
        "ProjectValhalla";  // The "who issued this token" label embedded in
                            // tokens.
    /** @brief Default token type. */
    const std::string TOKEN_TYPE_ = "JWS";  // JWS = JSON Web Signature, the
                                            // signed-token format used here.
    /** @brief Insecure development signing secret; override TOKEN_SECRET in
     * production. */
    const std::string TOKEN_SECRET_ =
        "01234567890123456789012345678901";  // Public placeholder key;
                                             // anyone knowing it could
                                             // forge tokens, so replace it.
  }  // namespace TokenParameters

  /**
   * @namespace Defaults::FrontEnd
   * @brief Default frontend host, port and invitation path.
   */
  namespace FrontEnd  // Where the browser-based frontend can be reached.
  {
    /** @brief Default frontend base URL. */
    const std::string HOST_ =
        "http://172.20.0.5";  // Hard-coded address of the frontend service
                              // on the development Docker network.
    /** @brief Default frontend port. */
    const uint16_t PORT_ =
        3000;  // Typical port for a JavaScript frontend development server.
    /** @brief Default path component for invitation links. */
    const std::string INVITE_PATH_ =
        "invite";  // Appended to the host to build
                   // invitation URLs sent to users.

  }  // namespace FrontEnd

  /**
   * @namespace Defaults::EmailSenderDaemon
   * @brief Default endpoint of the external email-sender daemon.
   */
  namespace EmailSenderDaemon  // Location of the separate service that
                               // delivers emails.
  {
    /** @brief Default daemon base URL. */
    const std::string HOST_ =
        "http://172.20.0.6";  // Address of the email-sender service on the
                              // development Docker network.
    /** @brief Default daemon port. */
    const uint16_t PORT_ = 5000;  // Port the email-sender service listens on.
    /** @brief Default HTTP path used to enqueue messages. */
    const std::string QUEUE_PATH_ =
        "/enqueue";  // Path the server calls to hand an
                     // outgoing message to the daemon.
  }  // namespace EmailSenderDaemon

  /**
   * @namespace Defaults::PermissionCache
   * @brief Default idle lifetime for cached permission decisions.
   */
  namespace PermissionCache  // How long permission-check results are
                             // remembered.
  {
    const uint32_t TTL_ = 300;  // seconds a cached permission may stay idle:
                                // 300 = 5 minutes; 0 would disable caching.
  }  // namespace PermissionCache

  /**
   * @namespace Defaults::PasswordWorker
   * @brief Default sizing of the password hashing/verification worker pool.
   */
  namespace PasswordWorker  // Background threads that do slow password
                            // hashing off the request path.
  {
    const uint16_t WORKERS_ =
        2;  // threads that hash and verify passwords; 0 = run on the
            // request thread (tests). Small unsigned count fits.
    const uint32_t QUEUE_MAX_ =
        64;  // waiting password operations before new ones get 503; bounds
             // memory and keeps delays predictable.
  }  // namespace PasswordWorker

  /**
   * @namespace Defaults::DbExecutor
   * @brief Default sizing/back-pressure for the database request executor.
   */
  namespace DbExecutor  // Worker/queue settings for the component that
                        // schedules database work.
  {
    const uint32_t QUEUE_MAX_ =
        256;  // waiting database requests before new ones get 503 (workers
              // default to DB_MAX_CONN); caps queued work.
    const bool IO_FAST_PATH_ =
        false;  // try each request on the IO thread first; only worth it
                // when many requests are refused without a query.
  }  // namespace DbExecutor

  /**
   * @namespace Defaults::Testing
   * @brief Defaults for test-only switches.
   */
  namespace Testing  // Switches used only by the test harness, never needed
                     // in normal production requests.
  {
    /** @brief Real database is used unless a test explicitly opts out. */
    const bool VALHALLA_TEST_NO_DB_ =
        false;  // false means tests use a real database; true lets a test
                // skip database setup entirely.
  }  // namespace Testing
};  // namespace Defaults
