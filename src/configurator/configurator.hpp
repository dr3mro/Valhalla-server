/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file configurator.hpp
 * @brief Environment-driven configuration sections and typed accessors for the
 * server.
 * @details This header defines Configurator, a container that owns one settings
 * object for each part of the server. Every settings object reads its values
 * from environment variables when it is created, using hard-coded defaults from
 * the Defaults namespace when a variable is missing or invalid. Callers
 * retrieve a settings object by asking for its type, for example
 * get<Configurator::ServerConfig>(). Most values are logged on startup with
 * secrets hidden, and a few counts are automatically sized from the machine's
 * CPU count.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the single home for ALL runtime settings. The Store owns one
 * Configurator and builds it before the Drogon IO threads start; then the
 * server, database, gatekeeper and worker components pull their settings from
 * it through get<T>(). Nothing else reads environment variables directly for
 * these options. Without this file, settings would be scattered and
 * inconsistent, and there would be no central place to print or override them.
 * Note that values are captured once at construction, so changing the
 * environment later has no effect.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Environment variables: https://man7.org/linux/man-pages/man7/environ.7.html
 * - C++ concepts and constraints:
 * https://en.cppreference.com/w/cpp/language/constraints
 * - Function templates:
 * https://en.cppreference.com/w/cpp/language/function_template
 * - std::chrono durations (lengths of time):
 * https://en.cppreference.com/w/cpp/chrono/duration
 * - std::unordered_set (a collection of unique values):
 * https://en.cppreference.com/w/cpp/container/unordered_set
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this file at most once per translation unit, no matter
              // how many times it is included.

// Core formatting machinery used by fmt::format to build log messages.
#include <fmt/core.h>
// Provides fmt::format, which fills {} placeholders with values for readable
// logs.
#include <fmt/format.h>
// Lets fmt print whole containers (such as the whitelist) in one call with
// fmt::join.
#include <fmt/ranges.h>

// std::chrono::seconds stores durations like timeouts and token lifetimes.
#include <chrono>
// Fixed-width integer types (uint8_t, uint16_t, uint32_t, uint64_t) with
// predictable sizes.
#include <cstdint>
// std::string holds textual settings such as host names and paths.
#include <string>
// std::is_base_of_v checks the inheritance relationship used by the Config
// concept.
#include <type_traits>
// std::unordered_set stores unique fingerprints in the whitelist and blacklist.
#include <unordered_set>

// Base class that converts environment strings into typed values with defaults.
#include "configurator/envloader/envloader.hpp"
// The fallback constants used whenever an environment variable is missing or
// invalid.
#include "defaults.hpp"
// Provides Message::ConfMessage/WarningMessage used to print settings on
// startup.
#include "utils/message/message.hpp"
/** @brief Git tag captured at build time.
 *
 * @details Besides identifying the build, this string doubles as the
 * environment-variable key consulted by Configurator::ServerConfig for an
 * optional version override.
 */
extern const char* const
    VALHALLA_GIT_TAG;  // A C string supplied by the build; also used as the
                       // env-var NAME for a version override.
/**
 * @brief Constrains the configuration-section types retrievable through
 * Configurator::get().
 * @tparam T Candidate section type; must derive from EnvLoader.
 */
template <typename T>  // T is the type being checked; "concept" means a
                       // compile-time rule a type must satisfy.
concept Config =
    std::is_base_of_v<EnvLoader,
                      T>;  // The rule: T must inherit from EnvLoader, so
                           // only real config sections are accepted.
/**
 * @class Configurator
 * @brief Owning aggregate of all runtime configuration sections.
 *
 * @details Holds one instance per configuration domain (server, database, DoS
 * detection, tokens, frontend, email sender, caches and workers) plus a testing
 * section. Every section derives from EnvLoader and reads its values from the
 * process environment in its constructor, falling back to the constants in the
 * @ref Defaults namespace when a variable is absent or invalid. The
 * Configurator is intended to be a single process-wide object owned by Store
 * and fully constructed before the Drogon IO threads start; sections are
 * retrieved by type through get<T>().
 *
 * @note Values are snapshotted at construction time, so later changes to the
 * environment do not affect an already-built Configurator.
 * @warning get() returns a non-const reference and performs no locking. Treat
 * sections as read-only once construction is complete; unsynchronized
 * concurrent mutation is unsafe.
 * @see EnvLoader, Defaults, Store
 */
class Configurator  // The single owner of every settings section; built once,
                    // then read by the rest of the server.
{
 public:  // Everything below is Configurator's public interface, callable by
          // other code.
  /** @brief Default-constructs every configuration section (each reads the
   * environment). */
  Configurator() = default;  // Let each member's own constructor run; that is
                             // where the environment is read.
  /** @brief Virtual destructor; derived configurators may extend the section
   * set. */
  virtual ~Configurator() =
      default;  // Virtual so a Configurator subclass can be destroyed
                // correctly through a base pointer.

  /**
   * @brief Logs the values of the nine non-test configuration sections.
   * @details Delegates to each section's printValues(). Secret-bearing fields
   * (database password, token secret) are logged as "[redacted]"; the token
   * secret additionally triggers a warning while it still equals the shipped
   * default. The test-only Testing section is deliberately not printed.
   * @note Diagnostic output only; no validation or mutation is performed.
   */
  void printValues() const  // Called at startup to show operators the effective
                            // settings; const so it cannot change them.
  {
    dos_detector_config_.printValues();  // Print DoS-detection limits first.
    server_config_.printValues();        // Print the HTTP server's identity,
                                         // networking and logging settings.
    database_config_
        .printValues();  // Print database settings, hiding the password.
    token_manager_parameters_
        .printValues();  // Print token settings, hiding the secret and
                         // warning if it is the default.
    frontend_config_.printValues();  // Print frontend address and CORS origins.
    email_sender_config_
        .printValues();  // Print where outgoing emails are handed off.
    permission_cache_config_
        .printValues();  // Print the permission cache lifetime.
    password_worker_config_
        .printValues();                 // Print password-worker pool sizing.
    db_executor_config_.printValues();  // Print database-executor sizing
                                        // and fast-path toggle.
  }

  /**
   * @struct DOSDetectorConfig
   * @brief Thresholds and durations used by the DoS detector / rate limiter.
   * @details All fields are read from the environment in the constructor;
   * @ref Defaults::DosDetector supplies the fallbacks. Durations are whole
   * seconds and the whitelist/blacklist are comma-separated sets of client
   * fingerprints.
   */
  using DOSDetectorConfig = struct DOSDetectorConfig
      : public EnvLoader  // Declares a struct and an alias of the same name,
                          // inheriting the env helpers.
  {
    /** @brief Maximum requests accepted within one @ref period before
     * limiting applies. */
    uint32_t max_requests;  // How many requests one client may make per period.
                            // 32-bit unsigned is plenty and cannot be negative.
    /** @brief Interval (seconds) between sweeps of expired rate-limit/ban
     * entries. */
    uint32_t clean_freq;  // Seconds between cleanups; unsigned because a
                          // negative interval is meaningless.
    /** @brief Upper bound on the number of distinct client fingerprints
     * retained. */
    uint32_t max_fingerprints;  // Caps how many unique clients are tracked,
                                // limiting memory use.
    /** @brief Length (seconds) of the sliding counting window for @ref
     * max_requests. */
    std::chrono::seconds
        period;  // A duration type makes the unit explicit; the detector
                 // counts requests over this window.
    /** @brief Duration (seconds) a client stays rate-limited. */
    std::chrono::seconds
        ratelimit_duration;  // How long excess requests are refused before
                             // the client is allowed again.
    /** @brief Duration (seconds) a banned client stays blocked. */
    std::chrono::seconds ban_duration;  // Longer penalty than rate limiting;
                                        // used for serious abusers.
    /** @brief Fingerprints exempt from limiting; parsed from a
     * comma-separated value. */
    std::unordered_set<std::string>
        whitelist;  // A set gives fast "is this client exempt?" checks and
                    // ignores duplicates.
    /** @brief Fingerprints always subject to limiting; parsed from a
     * comma-separated value. */
    std::unordered_set<std::string>
        blacklist;  // Clients here are always limited; a set keeps
                    // membership checks quick.

    /**
     * @brief Reads each DOS limit from its environment variable, using
     * Defaults on absence or parse failure.
     * @note Duration variables use EnvLoader's seconds overload, which
     * falls back to the default for negative or unparsable values.
     */
    DOSDetectorConfig()  // Initialises every field by reading its variable,
                         // with a Defaults fallback.
        : max_requests(
              getEnvironmentVariable("MAX_REQUESTS",
                                     Defaults::DosDetector::
                                         MAX_REQUESTS_)),  // MAX_REQUESTS
                                                           // overrides the
                                                           // request cap.
          clean_freq(
              getEnvironmentVariable("CLN_FRQ",
                                     Defaults::DosDetector::
                                         CLN_FRQ_)),  // CLN_FRQ = cleanup
                                                      // frequency in seconds.
          max_fingerprints(
              getEnvironmentVariable("MAX_FPS",
                                     Defaults::DosDetector::
                                         MAX_FPS_)),  // MAX_FPS caps tracked
                                                      // client fingerprints.
          period(
              getEnvironmentVariable("PERIOD",
                                     std::chrono::seconds(
                                         Defaults::DosDetector::
                                             PERIOD_))),  // PERIOD is wrapped
                                                          // as seconds so it
                                                          // matches the field
                                                          // type.
          ratelimit_duration(
              getEnvironmentVariable("RL_DURATION",
                                     std::chrono::seconds(
                                         Defaults::DosDetector::
                                             RL_DURATION_))),  // RL_DURATION =
                                                               // rate-limit
                                                               // length in
                                                               // seconds.
          ban_duration(getEnvironmentVariable("BAN_DURATION",
                                              std::chrono::seconds(
                                                  // BAN_DURATION = ban length
                                                  // in seconds.
                                                  Defaults::DosDetector::
                                                      BAN_DURATION_))),
          whitelist(getEnvironmentVariable(
              "WHITELIST")),  // Comma-separated list of exempt
                              // fingerprints; no default means an empty
                              // set.
          blacklist(getEnvironmentVariable(
              "BLACKLIST"))  // Comma-separated list of always-limited
                             // fingerprints; empty when unset.
    {
    }

    /** @brief Logs all DOS limits and both fingerprint sets. */
    void printValues() const override  // Required by EnvLoader; prints this
                                       // section's effective values.
    {
      Message::ConfMessage(
          // Decorative separator line for readable output.
          "------------------------------------------------");
      Message::ConfMessage(
          // Section title.
          "---------------DOS Detector Config--------------");
      Message::ConfMessage(
          // Decorative separator line.
          "------------------------------------------------");
      Message::ConfMessage(
          fmt::format("Max Requests: {}",
                      max_requests));  // Show the per-window request cap.
      Message::ConfMessage(
          fmt::format("Period: {} seconds",
                      period.count()));  // .count() returns the raw number of
                                         // seconds for printing.
      Message::ConfMessage(
          fmt::format("Max FPS: {}",
                      max_fingerprints));  // Show the fingerprint-tracking
                                           // limit.
      Message::ConfMessage(
          fmt::format("Rate Limit Duration: {} seconds",
                      ratelimit_duration
                          .count()));  // Show how long rate limiting lasts.
      Message::ConfMessage(
          fmt::format("Ban Duration: {} seconds",
                      ban_duration.count()));  // Show how long a ban lasts.
      Message::ConfMessage(fmt::format("Cleanup Frequency: {} seconds",
                                       clean_freq));  // Show how often expired
                                                      // entries are swept.
      Message::ConfMessage(
          fmt::format("Whitelist: {}",
                      fmt::join(whitelist,
                                ", ")));  // fmt::join prints the whole set
                                          // as one comma-separated string.
      Message::ConfMessage(
          fmt::format("Blacklist: {}",
                      fmt::join(blacklist, ", ")));  // Same for the blacklist.
    }
  };

  /**
   * @struct DatabaseConfig
   * @brief Connection parameters for the PostgreSQL database (and the
   * database executor).
   * @details Reads DB_* variables with @ref Defaults::Database fallbacks. A
   * zero max_conn is auto-sized to @c hardware_concurrency()*5 by the
   * constructor; the same sizing is applied independently in
   * DbExecutorConfig. Credentials are never logged.
   * @warning The shipped pass default is an insecure development placeholder;
   * override DB_PASS in any non-test deployment.
   */
  using DatabaseConfig = struct DatabaseConfig
      : public EnvLoader  // Settings for connecting to PostgreSQL, inheriting
                          // the env helpers.
  {
    /** @brief Whether the PostgreSQL connection uses SSL. */
    bool ssl;    // true means encrypt the connection; bool because it is a
                 // simple on/off choice.
    bool async;  // DB_ASYNC: asynchronous database access (Drogon DbClient)
                 // instead of the synchronous pool
    /** @brief Maximum pooled connections; zero means auto-size from the CPU
     * count. */
    uint16_t max_conn;  // Size of the connection pool; 16-bit unsigned is
                        // enough and zero is the "auto" sentinel.
    /** @brief Milliseconds a request waits for a free connection before a
     * 503. */
    uint32_t acquire_timeout_ms;  // Wait time in milliseconds; 32-bit
                                  // unsigned allows multi-second timeouts.
    /** @brief PostgreSQL listening port. */
    uint16_t port;  // Port number; 16-bit unsigned matches the network port
                    // range.
    /** @brief Database name. */
    std::string name;  // Name of the database to connect to; text because
                       // it is an identifier.
    /** @brief Database user. */
    std::string user;  // Login user; text because it is an identifier.
    /** @brief Database password; redacted in all diagnostic output. */
    std::string pass;  // Secret credential; never printed to protect it.
    /** @brief Database host address. */
    std::string host;  // Address of the database server; text because it
                       // can be a name or an IP.

    /**
     * @brief Reads DB_* variables, then auto-sizes @ref max_conn when it is
     * zero.
     * @details optimize_performance(max_conn, 5) replaces a zero connection
     * count with
     * @c hardware_concurrency()*5 so the pool scales with the machine by
     * default.
     */
    DatabaseConfig()  // Reads all DB_* variables then applies CPU-based
                      // pool sizing.
        : ssl(getEnvironmentVariable("DB_SSL",
                                     Defaults::Database::
                                         DB_SSL_)),  // DB_SSL toggles encrypted
                                                     // connections.
          async(
              getEnvironmentVariable("DB_ASYNC",
                                     Defaults::Database::
                                         DB_ASYNC_)),  // DB_ASYNC selects async
                                                       // queries over the
                                                       // synchronous pool.
          max_conn(
              getEnvironmentVariable("DB_MAX_CONN",
                                     Defaults::Database::
                                         DB_MAX_CONN_)),  // DB_MAX_CONN sets
                                                          // the pool size, zero
                                                          // means auto.
          acquire_timeout_ms(getEnvironmentVariable(
              "DB_ACQUIRE_TIMEOUT_MS",
              Defaults::Database::DB_ACQUIRE_TIMEOUT_MS_)),  // Timeout
                                                             // when no
                                                             // connection
                                                             // is free.
          port(getEnvironmentVariable("DB_PORT",
                                      Defaults::Database::
                                          DB_PORT_)),  // DB_PORT overrides
                                                       // PostgreSQL's default
                                                       // port.
          name(getEnvironmentVariable("DB_NAME",
                                      Defaults::Database::
                                          DB_NAME_)),  // DB_NAME selects the
                                                       // database.
          user(getEnvironmentVariable("DB_USER",
                                      Defaults::Database::
                                          DB_USER_)),  // DB_USER selects the
                                                       // login role.
          pass(getEnvironmentVariable("DB_PASS",
                                      Defaults::Database::
                                          DB_PASS_)),  // DB_PASS supplies the
                                                       // secret, never logged.
          host(getEnvironmentVariable("DB_HOST",
                                      Defaults::Database::
                                          DB_HOST_))  // DB_HOST selects the
                                                      // server address.
    {
      optimize_performance(max_conn,
                           5);  // Turn a zero pool size into roughly five
                                // connections per CPU core.
    }

    /** @brief Logs connection settings; the password is shown as
     * "[redacted]". */
    void printValues() const override  // Prints connection settings while
                                       // protecting the password.
    {
      Message::ConfMessage(
          // Decorative separator line.
          "------------------------------------------------");
      Message::ConfMessage(
          // Section title.
          "-----------------Database Config----------------");
      Message::ConfMessage(
          // Decorative separator line.
          "------------------------------------------------");
      Message::ConfMessage(
          fmt::format("Host: {}", host));  // Show the database address.
      Message::ConfMessage(fmt::format("Port: {}", port));  // Show the port.
      Message::ConfMessage(
          fmt::format("Name: {}", name));  // Show the database name.
      Message::ConfMessage(
          fmt::format("User: {}", user));        // Show the login user.
      Message::ConfMessage("Pass: [redacted]");  // Deliberately hide the secret
                                                 // instead of printing it.
      Message::ConfMessage(
          fmt::format("SSL: {}",
                      ssl));  // Show whether connections are encrypted.
      Message::ConfMessage(fmt::format("Async: {}",
                                       async));  // Show whether async database
                                                 // access is enabled.
      Message::ConfMessage(
          fmt::format("Max Connections: {}",
                      max_conn));  // Show the effective pool size.
      Message::ConfMessage(
          fmt::format("Connection Wait Timeout: {} ms",
                      acquire_timeout_ms));  // Show the wait timeout with its
                                             // unit.
    }
  };

  /**
   * @struct ServerConfig
   * @brief Drogon server identity, networking, logging and upload settings.
   * @details A zero thread count is auto-sized to @c
   * hardware_concurrency()*4. The ver field is looked up using the build-time
   * git tag as the environment-variable name, allowing a deployment to
   * override the reported version.
   * @see VALHALLA_GIT_TAG
   */
  using ServerConfig = struct ServerConfig
      : public EnvLoader  // Settings for the Drogon HTTP server itself.
  {
    /** @brief Interface the HTTP server binds to. */
    std::string host;  // Network interface to listen on; text because it is
                       // an address such as 0.0.0.0.
    /** @brief Logical server name advertised to clients. */
    std::string name;  // Human-facing name of the server; text because it
                       // is a label.
    /** @brief Version string; key for the override is the build git tag. */
    std::string ver;  // Reported version; the env-var name used to override
                      // it is the build's git tag.
    /** @brief Human-readable server description. */
    std::string desc;  // Short description of the service; text because it
                       // is prose.
    /** @brief HTTP listening port. */
    uint16_t port;  // Port clients connect to; 16-bit unsigned covers the
                    // valid port range.
    /** @brief Number of IO threads; zero means auto-size from the CPU
     * count. */
    uint16_t threads;      // Count of threads serving network events; zero is
                           // the "auto" sentinel.
    uint8_t  debug_level;  // 0 Debug 1 info 2 warning 3 error 4 critical
    /** @brief Whether log messages are written to the console. */
    bool log_to_console;  // true prints logs to the terminal; bool because
                          // it is on/off.
    /** @brief Whether log messages are written to a file. */
    bool log_to_file;  // true also saves logs to disk; bool because it is
                       // on/off.
    /** @brief Directory for log files. */
    std::string log_dir;  // Folder where log files are written; text
                          // because it is a path.
    /** @brief Log file name. */
    std::string log_file;  // Name of the log file inside log_dir; text
                           // because it is a filename.
    /** @brief Directory holding uploaded files. */
    std::string upload_dir;  // Folder for files clients upload; text
                             // because it is a path.

    /**
     * @brief Reads SERVER_* variables, then auto-sizes @ref threads when it
     * is zero.
     * @details optimize_performance(threads, 4) derives the default IO
     * thread count from
     * @c hardware_concurrency().
     */
    ServerConfig()  // Reads all SERVER_* variables then applies CPU-based
                    // thread sizing.
        : host(
              getEnvironmentVariable("SERVER_HOST",
                                     Defaults::Server::
                                         SERVER_HOST_)),  // SERVER_HOST selects
                                                          // the bind interface.
          name(getEnvironmentVariable("SERVER_NAME",
                                      Defaults::Server::
                                          SERVER_NAME_)),  // SERVER_NAME sets
                                                           // the advertised
                                                           // name.
          ver(getEnvironmentVariable(VALHALLA_GIT_TAG,
                                     Defaults::Server::
                                         SERVER_VER_)),  // The git tag is the
                                                         // env-var NAME, so a
                                                         // build can override
                                                         // its version.
          desc(getEnvironmentVariable("SERVER_DESCRIPTION",
                                      Defaults::Server::
                                          SERVER_DESC_)),  // SERVER_DESCRIPTION
                                                           // sets the readable
                                                           // description.
          port(
              getEnvironmentVariable("SERVER_PORT",
                                     Defaults::Server::
                                         SERVER_PORT_)),  // SERVER_PORT selects
                                                          // the HTTP port.
          threads(
              getEnvironmentVariable("SERVER_THREADS",
                                     Defaults::Server::
                                         SERVER_THREADS_)),  // SERVER_THREADS
                                                             // sets the IO
                                                             // thread count,
                                                             // zero means auto.
          debug_level(
              getEnvironmentVariable("SERVER_DEBUG_LEVEL",
                                     // SERVER_DEBUG_LEVEL sets log verbosity.
                                     Defaults::Server::SERVER_DEBUG_LEVEL_)),
          log_to_console(
              getEnvironmentVariable("SERVER_LOG_TO_CONSOLE",
                                     // Toggles logging to the terminal.
                                     Defaults::Server::SERVER_LOG_TO_CONSOLE_)),
          log_to_file(
              getEnvironmentVariable("SERVER_LOG_TO_FILE",
                                     Defaults::Server::
                                         SERVER_LOG_TO_FILE_)),  // Toggles
                                                                 // logging to
                                                                 // disk.
          log_dir(getEnvironmentVariable("SERVER_LOG_DIR",
                                         Defaults::Server::
                                             SERVER_LOG_DIR_)),  // Sets the log
                                                                 // directory.
          log_file(
              getEnvironmentVariable("SERVER_LOG_FILE",
                                     Defaults::Server::
                                         SERVER_LOG_FILE_)),  // Sets the log
                                                              // file name.
          upload_dir(
              getEnvironmentVariable("SERVER_UPLOAD_DIR",
                                     Defaults::Server::
                                         SERVER_UPLOAD_DIR_))  // Sets where
                                                               // uploads are
                                                               // stored.
    {
      optimize_performance(threads,
                           4);  // Turn a zero thread count into roughly four IO
                                // threads per CPU core.
    }

    /** @brief Logs server identity, networking, threading and logging
     * settings. */
    void printValues()
        const override  // Prints the server's effective settings.
    {
      Message::ConfMessage(
          // Decorative separator line.
          "------------------------------------------------");
      Message::ConfMessage(
          // Section title.
          "-------------------ServerConfig-----------------");
      Message::ConfMessage(
          // Decorative separator line.
          "------------------------------------------------");
      Message::ConfMessage(
          fmt::format("Host: {}", host));  // Show the bind address.
      Message::ConfMessage(
          fmt::format("Port: {}", port));  // Show the HTTP port.
      Message::ConfMessage(
          fmt::format("Name: {}", name));  // Show the server name.
      Message::ConfMessage(
          fmt::format("Version: {}", ver));  // Show the reported version.
      Message::ConfMessage(
          fmt::format("Description: {}", desc));  // Show the description.
      Message::ConfMessage(
          fmt::format("Threads: {}", threads));  // Show the IO thread count.
      Message::ConfMessage(
          fmt::format("Debug Level: {}",
                      debug_level));  // Show the log verbosity level.
      Message::ConfMessage(
          fmt::format("Log to console: {}",
                      log_to_console));  // Show console logging state.
      Message::ConfMessage(
          fmt::format("Log to file: {}",
                      log_to_file));  // Show file logging state.
      Message::ConfMessage(
          fmt::format("Log directory: {}", log_dir));  // Show the log folder.
      Message::ConfMessage(
          fmt::format("Log file: {}", log_file));  // Show the log filename.
      Message::ConfMessage(fmt::format("Upload directory: {}",
                                       upload_dir));  // Show the upload folder.
    }
  };

  /**
   * @struct TokenManagerParameters
   * @brief Parameters governing issuance and verification of auth tokens.
   * @details @c validity is measured in minutes. The secret is redacted when
   * logged and a warning is emitted while it equals the default, since that
   * default is public.
   * @warning The shipped secret is an insecure development placeholder; set
   * TOKEN_SECRET to a secure value in production.
   */
  using TokenManagerParameters = struct TokenManagerParameters
      : public EnvLoader  // Settings for signing and checking authentication
                          // tokens.
  {
    /** @brief Token lifetime in minutes. */
    uint64_t validity;  // How long a token stays valid, in minutes; 64-bit
                        // because the default is large.
    /** @brief Issuer claim embedded in issued tokens. */
    std::string issuer;  // The "issued by" label inside tokens; text
                         // because it is an identifier.
    /** @brief Token type identifier (e.g. JWS). */
    std::string
        type;  // Format/type label such as JWS; text because it is a name.
    /** @brief Signing secret; redacted in all diagnostic output. */
    std::string secret;  // Key used to sign tokens; hidden in logs because
                         // revealing it lets attackers forge tokens.

    /** @brief Reads TOKEN_* variables, using @ref Defaults::TokenParameters
     * on absence. */
    TokenManagerParameters()  // Reads all TOKEN_* variables with the
                              // Defaults fallbacks.
        : validity(
              getEnvironmentVariable("TOKEN_VALIDITY",
                                     Defaults::TokenParameters::
                                         TOKEN_VALIDITY_)),  // TOKEN_VALIDITY
                                                             // sets token
                                                             // lifetime in
                                                             // minutes.
          issuer(
              getEnvironmentVariable("TOKEN_ISSUER",
                                     Defaults::TokenParameters::
                                         TOKEN_ISSUER_)),  // TOKEN_ISSUER sets
                                                           // the issuer label.
          type(getEnvironmentVariable("TOKEN_TYPE",
                                      Defaults::TokenParameters::
                                          TOKEN_TYPE_)),  // TOKEN_TYPE sets the
                                                          // type identifier.
          secret(getEnvironmentVariable("TOKEN_SECRET",
                                        Defaults::TokenParameters::
                                            TOKEN_SECRET_))  // TOKEN_SECRET
                                                             // supplies the
                                                             // signing key,
                                                             // never logged.
    {
    }

    /**
     * @brief Logs issuer, type and validity; the secret is shown as
     * "[redacted]".
     * @details Emits a warning when the secret still equals the shipped
     * default.
     */
    void printValues()
        const override  // Prints token settings, hiding the secret and
                        // warning if it is still the default.
    {
      Message::ConfMessage(
          // Decorative separator line.
          "------------------------------------------------");
      Message::ConfMessage(
          // Section title.
          "-------------TokenManagerParameters-------------");
      Message::ConfMessage(
          // Decorative separator line.
          "------------------------------------------------");
      Message::ConfMessage(
          fmt::format("Issuer: {}", issuer));  // Show the issuer label.
      Message::ConfMessage(
          fmt::format("Type: {}", type));  // Show the token type.
      Message::ConfMessage(
          fmt::format("Validity: {} minutes",
                      validity));  // Show lifetime with its unit.
      Message::ConfMessage("Secret: [redacted]");  // Hide the secret instead of
                                                   // printing it.
      if (secret ==
          Defaults::TokenParameters::
              TOKEN_SECRET_)  // Compare against the public placeholder to
                              // detect an insecure deployment.
      {
        Message::WarningMessage(
            "TOKEN_SECRET is set to the default value; set a secure "
            "secret for production.");  // Warn operators to replace the
                                        // well-known key.
      }
    }
  };

  /**
   * @struct FrontEndConfig
   * @brief Frontend reachability and CORS settings.
   * @details @c cors_allowed_origins is the raw value sent in the CORS
   * response; the default wildcard permits any origin and should be tightened
   * in production.
   */
  using FrontEndConfig = struct FrontEndConfig
      : public EnvLoader  // Settings for reaching the browser frontend and
                          // allowing CORS requests.
  {
    /** @brief Frontend HTTP port. */
    uint16_t port;  // Port where the frontend runs; 16-bit unsigned covers
                    // the port range.
    /** @brief Frontend base host/URL. */
    std::string
        host;  // Base address of the frontend; text because it is a URL.
    /** @brief Path template used for invitation links. */
    std::string invite_path;  // Path appended to build invitation links;
                              // text because it is a URL fragment.
    /** @brief Raw Access-Control-Allow-Origin value; "*" allows every
     * origin. */
    std::string cors_allowed_origins;  // CORS header value sent to browsers;
                                       // text because it is a header string.

    /** @brief Reads FRONTEND_* and CORS_* variables with @ref
     * Defaults::FrontEnd fallbacks. */
    FrontEndConfig()  // Reads frontend settings and the CORS origin value.
        : port(getEnvironmentVariable("FRONTEND_PORT",
                                      Defaults::FrontEnd::
                                          PORT_)),  // FRONTEND_PORT sets the
                                                    // frontend port.
          host(getEnvironmentVariable("FRONTEND_HOST",
                                      Defaults::FrontEnd::
                                          HOST_)),  // FRONTEND_HOST sets the
                                                    // frontend base URL.
          invite_path(getEnvironmentVariable(
              "FRONTEND_INVITE_PATH",
              Defaults::FrontEnd::INVITE_PATH_)),  // FRONTEND_INVITE_PATH
                                                   // sets the invite link
                                                   // path.
          cors_allowed_origins(
              getEnvironmentVariable("CORS_ALLOWED_ORIGINS",
                                     std::string(
                                         "*")))  // Defaults to "*" (allow all
                                                 // origins); wrap the literal
                                                 // in std::string to match the
                                                 // field type.
    {
    }

    /** @brief Logs frontend host, port, invite path and allowed CORS
     * origins. */
    void printValues()
        const override  // Prints the frontend's effective settings.
    {
      Message::ConfMessage(
          // Decorative separator line.
          "------------------------------------------------");
      Message::ConfMessage(
          // Section title.
          "------------------FrontEnd Config----------------");
      Message::ConfMessage(
          // Decorative separator line.
          "------------------------------------------------");
      Message::ConfMessage(
          fmt::format("Frontend host: {}", host));  // Show the frontend URL.
      Message::ConfMessage(
          fmt::format("Frontend port: {}", port));  // Show the frontend port.
      Message::ConfMessage(fmt::format("Frontend invite path: {}",
                                       invite_path));  // Show the invite path.
      Message::ConfMessage(
          fmt::format("CORS allowed origins: {}",
                      cors_allowed_origins));  // Show which origins browsers
                                               // may allow.
    }
  };

  /**
   * @struct EmailSenderConfig
   * @brief Endpoint and queue path of the external email-sender daemon.
   * @details Message delivery is delegated to that daemon; this section only
   * holds where to reach it and which queue endpoint to enqueue onto.
   */
  using EmailSenderConfig = struct EmailSenderConfig
      : public EnvLoader  // Settings for contacting the separate
                          // email-delivery service.
  {
    /** @brief Email-sender daemon port. */
    uint16_t port;  // Port of the email daemon; 16-bit unsigned fits a port
                    // number.
    /** @brief Email-sender daemon host/URL. */
    std::string host;  // Address of the email daemon; text because it is a URL.
    /** @brief HTTP path used to enqueue outgoing messages. */
    std::string message_queue_path;  // Path called to hand off an email;
                                     // text because it is a URL path.

    /** @brief Reads EMAIL_SENDER_* variables with @ref
     * Defaults::EmailSenderDaemon fallbacks. */
    EmailSenderConfig()  // Reads all EMAIL_SENDER_* variables with Defaults
                         // fallbacks.
        : port(getEnvironmentVariable("EMAIL_SENDER_PORT",
                                      Defaults::EmailSenderDaemon::
                                          PORT_)),  // EMAIL_SENDER_PORT sets
                                                    // the daemon port.
          host(getEnvironmentVariable("EMAIL_SENDER_HOST",
                                      Defaults::EmailSenderDaemon::
                                          HOST_)),  // EMAIL_SENDER_HOST sets
                                                    // the daemon address.
          message_queue_path(
              getEnvironmentVariable("EMAIL_SENDER_QUEUE_PATH",
                                     // EMAIL_SENDER_QUEUE_PATH sets the enqueue
                                     // path.
                                     Defaults::EmailSenderDaemon::QUEUE_PATH_))
    {
    }

    /** @brief Logs the email-sender host, port and queue path. */
    void printValues()
        const override  // Prints the email-sender's effective settings.
    {
      Message::ConfMessage(
          // Decorative separator line.
          "------------------------------------------------");
      Message::ConfMessage(
          // Section title.
          "----------------EmailSender Config--------------");
      Message::ConfMessage(
          // Decorative separator line.
          "------------------------------------------------");
      Message::ConfMessage(fmt::format("EmailSender host: {}",
                                       host));  // Show the daemon address.
      Message::ConfMessage(
          fmt::format("EmailSender port: {}", port));  // Show the daemon port.
      Message::ConfMessage(
          fmt::format("EmailSender queue path: {}",
                      message_queue_path));  // Show the enqueue path.
    }
  };

  /**
   * @struct PermissionCacheConfig
   * @brief Lifetime of cached permission decisions.
   * @details A zero `ttl` disables caching entirely. The TTL is an idle
   * lifetime: an unused cached permission may be evicted after this many
   * seconds.
   */
  using PermissionCacheConfig = struct PermissionCacheConfig
      : public EnvLoader  // Settings for how long permission-check results
                          // are remembered.
  {
    std::chrono::seconds
        ttl;  // idle lifetime of a cached permission; 0 disables the cache

    /** @brief Reads PERMISSION_CACHE_TTL (seconds), defaulting to @ref
     * Defaults::PermissionCache. */
    PermissionCacheConfig()
        : ttl(getEnvironmentVariable("PERMISSION_CACHE_TTL",
                                     std::chrono::seconds(
                                         Defaults::PermissionCache::TTL_)))
    {
    }  // Reads the TTL as seconds and wraps the default in the same
       // duration type.
    /** @brief Logs the configured idle TTL in seconds. */
    void printValues() const override  // Prints the cache's effective lifetime.
    {
      Message::ConfMessage(
          // Decorative separator line.
          "------------------------------------------------");
      Message::ConfMessage(
          // Section title.
          "-------------Permission Cache Config------------");
      Message::ConfMessage(
          // Decorative separator line.
          "------------------------------------------------");
      Message::ConfMessage(fmt::format("Permission cache TTL: {} seconds",
                                       ttl.count()));  // .count() gives the raw
                                                       // seconds for printing.
    }
  };

  /**
   * @struct PasswordWorkerConfig
   * @brief Sizing and back-pressure for the password hashing/verification
   * worker pool.
   * @details Work is offloaded to `workers` background threads so
   * bcrypt/argon-style work never blocks a Drogon IO thread. Once `queue_max`
   * operations are waiting, new ones are refused with HTTP 503.
   */
  using PasswordWorkerConfig = struct PasswordWorkerConfig
      : public EnvLoader  // Settings for the slow password-hashing worker
                          // pool.
  {
    uint16_t workers;    // 0 runs password work on the request thread
    uint32_t queue_max;  // queued operations before a new one is refused
                         // with 503

    /** @brief Reads PASSWORD_WORKERS and PASSWORD_QUEUE_MAX with Defaults
     * fallbacks. */
    PasswordWorkerConfig()  // Reads the worker count and queue capacity.
        : workers(
              getEnvironmentVariable("PASSWORD_WORKERS",
                                     Defaults::PasswordWorker::
                                         WORKERS_)),  // PASSWORD_WORKERS sets
                                                      // the thread count; 0
                                                      // means run inline.
          queue_max(
              getEnvironmentVariable("PASSWORD_QUEUE_MAX",
                                     Defaults::PasswordWorker::
                                         QUEUE_MAX_))  // PASSWORD_QUEUE_MAX
                                                       // bounds queued work
                                                       // before rejecting with
                                                       // 503.
    {
    }

    /** @brief Logs the worker count and queue capacity. */
    void printValues()
        const override  // Prints the password worker's effective settings.
    {
      Message::ConfMessage(
          // Decorative separator line.
          "------------------------------------------------");
      Message::ConfMessage(
          // Section title.
          "-------------Password Worker Config-------------");
      Message::ConfMessage(
          // Decorative separator line.
          "------------------------------------------------");
      Message::ConfMessage(
          fmt::format("Password workers: {}",
                      workers));  // Show how many hashing threads exist.
      Message::ConfMessage(fmt::format("Password queue max: {}",
                                       queue_max));  // Show the queue capacity.
    }
  };

  /**
   * @struct DbExecutorConfig
   * @brief Sizing and fast-path toggle for the database request executor.
   * @details `workers` defaults to the same value as DatabaseConfig::max_conn
   * (one worker per connection); it can be overridden with DB_WORKERS. At
   * `queue_max` waiting requests, new submissions are refused with HTTP 503.
   * The optional `io_fast_path` attempts requests on the IO thread first,
   * which only helps when many requests are rejected without ever issuing a
   * query.
   */
  using DbExecutorConfig = struct DbExecutorConfig
      : public EnvLoader  // Settings for the component that schedules
                          // database work.
  {
    uint16_t workers;    // 0 runs database work on the request thread (tests)
    uint32_t queue_max;  // queued requests before a new one is refused with 503
    bool     io_fast_path;  // answer requests that need no database on the IO
                            // thread (DB_IO_FASTPATH)

    /**
     * @brief Derives a default worker count from the configured connection
     * pool size.
     * @details Reads DB_MAX_CONN (defaulting to @ref
     * Defaults::Database::DB_MAX_CONN_), applies the same @c
     * hardware_concurrency()*5 auto-sizing used by DatabaseConfig, and
     * returns the result as the initial `workers` value.
     */
    DbExecutorConfig()  // Starts workers from the DB pool size, then allows
                        // an explicit override.
        : workers(defaultWorkers()),  // Use the helper so worker count tracks
                                      // the connection pool by default.
          queue_max(
              getEnvironmentVariable("DB_QUEUE_MAX",
                                     Defaults::DbExecutor::
                                         QUEUE_MAX_)),  // DB_QUEUE_MAX bounds
                                                        // queued database work
                                                        // before rejecting with
                                                        // 503.
          io_fast_path(
              getEnvironmentVariable("DB_IO_FASTPATH",
                                     Defaults::DbExecutor::
                                         IO_FAST_PATH_))  // DB_IO_FASTPATH
                                                          // toggles the
                                                          // IO-thread shortcut.
    {
      workers =
          getEnvironmentVariable("DB_WORKERS",
                                 workers);  // DB_WORKERS, if set, replaces the
                                            // derived default with an explicit
                                            // count.
    }

    /** @brief Logs worker count, queue capacity and fast-path setting. */
    void printValues()
        const override  // Prints the database executor's effective settings.
    {
      Message::ConfMessage(
          // Decorative separator line.
          "------------------------------------------------");
      Message::ConfMessage(
          // Section title.
          "---------------DB Executor Config---------------");
      Message::ConfMessage(
          // Decorative separator line.
          "------------------------------------------------");
      Message::ConfMessage(
          fmt::format("DB workers: {}",
                      workers));  // Show how many database workers exist.
      Message::ConfMessage(fmt::format("DB queue max: {}",
                                       queue_max));  // Show the queue capacity.
      Message::ConfMessage(
          fmt::format("DB IO fast path: {}",
                      io_fast_path));  // Show whether the IO-thread shortcut is
                                       // on.
    }

   private:  // The helper below is internal to this section; outside
             // callers cannot invoke it.
    // One worker per database connection, using the same pool size the
    // connection pool will use.
    uint16_t defaultWorkers()  // Computes a worker count that matches the
                               // connection pool size.
    {
      auto pool = static_cast<uint16_t>(
          getEnvironmentVariable("DB_MAX_CONN",
                                 static_cast<uint16_t>(
                                     Defaults::Database::
                                         DB_MAX_CONN_)));  // Read DB_MAX_CONN,
                                                           // convert to a
                                                           // 16-bit count, and
                                                           // wrap for the
                                                           // generic getter.
      optimize_performance(pool,
                           5);  // Apply the same zero-to-CPU-times-five sizing
                                // used for the real connection pool.
      return pool;              // Hand back the derived count so workers and
                                // connections stay balanced.
    }
  };

  /**
   * @struct Testing
   * @brief Switches used only by the test harness.
   * @details @c no_db lets tests start the server without a real database
   * connection. It defaults to false and is never relied upon by production
   * request handling.
   */
  using Testing =
      struct Testing : public EnvLoader  // Test-only switch section; not
                                         // printed with the production config.
  {
   public:  // The switch field and its constructor/logger are public, like
            // the other sections.
    bool no_db;
    /*NOLINT */  // true lets tests run without a real database; false means
                 // use the real DB. The NOLINT marker silences a linter
                 // warning about
    // the short name.

    /** @brief Reads VALHALLA_TEST_NO_DB with @ref Defaults::Testing
     * fallback. */
    Testing()
        : no_db(getEnvironmentVariable("VALHALLA_TEST_NO_DB",
                                       Defaults::Testing::VALHALLA_TEST_NO_DB_))
    {
    }  // Reads the opt-out switch, defaulting to false so tests use the
       // real database.
    /** @brief Logs the test-only no_db switch. */
    void printValues() const override  // Prints the single test-only switch
                                       // (normally not called in production).
    {
      Message::ConfMessage(
          // Decorative separator line.
          "------------------------------------------------");
      Message::ConfMessage(
          // Section title.
          "-----------------Testing Config-----------------");
      Message::ConfMessage(
          // Decorative separator line.
          "------------------------------------------------");
      Message::ConfMessage(
          fmt::format("Testing no_db: {}",
                      no_db));  // Show whether the database is being skipped.
    }
  };

  // Template getter for structs

  /**
   * @brief Returns the configuration section of type T.
   * @tparam T A section type satisfying Config; explicit specializations in
   * configurator.cpp map each supported type to its member.
   * @return Reference to the corresponding section member.
   * @note Only the ten specialized types are retrievable; instantiating
   * get<T>() for any other Config type compiles but fails to link.
   * @warning The returned reference is mutable and unsynchronized; do not
   * modify it after construction.
   */
  template <Config T>  // T is constrained by the Config concept, so only
                       // EnvLoader-derived sections are allowed.
  T& get();            // Declared here but defined in
                       // configurator.cpp as one specialization
                       // per section type.

 private:  // The owned section objects are internal; callers reach them only
           // through get<T>().
  /** @brief Owned configuration sections, populated once at construction. */
  ServerConfig      server_config_;    // The server's own settings (host, port,
                                       // threads, logging).
  DatabaseConfig    database_config_;  // PostgreSQL connection settings.
  DOSDetectorConfig dos_detector_config_;  // Rate-limit and ban thresholds.
  TokenManagerParameters
                    token_manager_parameters_;  // Auth-token issuance settings.
  FrontEndConfig    frontend_config_;  // Frontend address and CORS settings.
  EmailSenderConfig email_sender_config_;          // Email daemon endpoint.
  PermissionCacheConfig permission_cache_config_;  // Permission cache lifetime.
  PasswordWorkerConfig
                   password_worker_config_;  // Password hashing pool sizing.
  DbExecutorConfig db_executor_config_;      // Database executor sizing.
  Testing          testing_;                 // Test-only switches.
};
