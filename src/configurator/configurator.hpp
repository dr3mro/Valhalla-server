#pragma once

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <type_traits>
#include <unordered_set>

#include "configurator/envloader/envloader.hpp"
#include "defaults.hpp"
#include "utils/message/message.hpp"
extern const char* const VALHALLA_GIT_TAG;
template <typename T>
concept Config = std::is_base_of_v<EnvLoader, T>;

class Configurator
{
   public:
    Configurator()          = default;
    virtual ~Configurator() = default;

    void printValues() const
    {
        dos_detector_config_.printValues();
        server_config_.printValues();
        database_config_.printValues();
        token_manager_parameters_.printValues();
        frontend_config_.printValues();
        email_sender_config_.printValues();
        permission_cache_config_.printValues();
        password_worker_config_.printValues();
        db_executor_config_.printValues();
    }

    using DOSDetectorConfig = struct DOSDetectorConfig : public EnvLoader
    {
        uint32_t                        max_requests;
        uint32_t                        clean_freq;
        uint32_t                        max_fingerprints;
        std::chrono::seconds            period;
        std::chrono::seconds            ratelimit_duration;
        std::chrono::seconds            ban_duration;
        std::unordered_set<std::string> whitelist;
        std::unordered_set<std::string> blacklist;

        DOSDetectorConfig()
            : max_requests(getEnvironmentVariable("MAX_REQUESTS", Defaults::DosDetector::MAX_REQUESTS_)),
              clean_freq(getEnvironmentVariable("CLN_FRQ", Defaults::DosDetector::CLN_FRQ_)),
              max_fingerprints(getEnvironmentVariable("MAX_FPS", Defaults::DosDetector::MAX_FPS_)),
              period(getEnvironmentVariable("PERIOD", std::chrono::seconds(Defaults::DosDetector::PERIOD_))),
              ratelimit_duration(getEnvironmentVariable("RL_DURATION", std::chrono::seconds(Defaults::DosDetector::RL_DURATION_))),
              ban_duration(getEnvironmentVariable("BAN_DURATION", std::chrono::seconds(Defaults::DosDetector::BAN_DURATION_))),
              whitelist(getEnvironmentVariable("WHITELIST")),
              blacklist(getEnvironmentVariable("BLACKLIST"))
        {
        }

        void printValues() const override
        {
            Message::ConfMessage("------------------------------------------------");
            Message::ConfMessage("---------------DOS Detector Config--------------");
            Message::ConfMessage("------------------------------------------------");
            Message::ConfMessage(fmt::format("Max Requests: {}", max_requests));
            Message::ConfMessage(fmt::format("Period: {} seconds", period.count()));
            Message::ConfMessage(fmt::format("Max FPS: {}", max_fingerprints));
            Message::ConfMessage(fmt::format("Rate Limit Duration: {} seconds", ratelimit_duration.count()));
            Message::ConfMessage(fmt::format("Ban Duration: {} seconds", ban_duration.count()));
            Message::ConfMessage(fmt::format("Cleanup Frequency: {} seconds", clean_freq));
            Message::ConfMessage(fmt::format("Whitelist: {}", fmt::join(whitelist, ", ")));
            Message::ConfMessage(fmt::format("Blacklist: {}", fmt::join(blacklist, ", ")));
        }
    };

    using DatabaseConfig = struct DatabaseConfig : public EnvLoader
    {
        bool        ssl;
        bool        async;  // DB_ASYNC: asynchronous database access (Drogon DbClient) instead of the synchronous pool
        uint16_t    max_conn;
        uint32_t    acquire_timeout_ms;
        uint16_t    port;
        std::string name;
        std::string user;
        std::string pass;
        std::string host;

        DatabaseConfig()
            : ssl(getEnvironmentVariable("DB_SSL", Defaults::Database::DB_SSL_)),
              async(getEnvironmentVariable("DB_ASYNC", Defaults::Database::DB_ASYNC_)),
              max_conn(getEnvironmentVariable("DB_MAX_CONN", Defaults::Database::DB_MAX_CONN_)),
              acquire_timeout_ms(getEnvironmentVariable("DB_ACQUIRE_TIMEOUT_MS", Defaults::Database::DB_ACQUIRE_TIMEOUT_MS_)),
              port(getEnvironmentVariable("DB_PORT", Defaults::Database::DB_PORT_)),
              name(getEnvironmentVariable("DB_NAME", Defaults::Database::DB_NAME_)),
              user(getEnvironmentVariable("DB_USER", Defaults::Database::DB_USER_)),
              pass(getEnvironmentVariable("DB_PASS", Defaults::Database::DB_PASS_)),
              host(getEnvironmentVariable("DB_HOST", Defaults::Database::DB_HOST_))
        {
            optimize_performance(max_conn, 5);
        }

        void printValues() const override
        {
            Message::ConfMessage("------------------------------------------------");
            Message::ConfMessage("-----------------Database Config----------------");
            Message::ConfMessage("------------------------------------------------");
            Message::ConfMessage(fmt::format("Host: {}", host));
            Message::ConfMessage(fmt::format("Port: {}", port));
            Message::ConfMessage(fmt::format("Name: {}", name));
            Message::ConfMessage(fmt::format("User: {}", user));
            Message::ConfMessage(fmt::format("Pass: {}", pass));
            Message::ConfMessage(fmt::format("SSL: {}", ssl));
            Message::ConfMessage(fmt::format("Async: {}", async));
            Message::ConfMessage(fmt::format("Max Connections: {}", max_conn));
            Message::ConfMessage(fmt::format("Connection Wait Timeout: {} ms", acquire_timeout_ms));
        }
    };

    using ServerConfig = struct ServerConfig : public EnvLoader
    {
        std::string host;
        std::string name;
        std::string ver;
        std::string desc;
        uint16_t    port;
        uint16_t    threads;
        uint8_t     debug_level;  // 0 Debug 1 info 2 warning 3 error 4 critical
        bool        log_to_console;
        bool        log_to_file;
        std::string log_dir;
        std::string log_file;
        std::string upload_dir;

        ServerConfig()
            : host(getEnvironmentVariable("SERVER_HOST", Defaults::Server::SERVER_HOST_)),
              name(getEnvironmentVariable("SERVER_NAME", Defaults::Server::SERVER_NAME_)),
              ver(getEnvironmentVariable(VALHALLA_GIT_TAG, Defaults::Server::SERVER_VER_)),
              desc(getEnvironmentVariable("SERVER_DESCRIPTION", Defaults::Server::SERVER_DESC_)),
              port(getEnvironmentVariable("SERVER_PORT", Defaults::Server::SERVER_PORT_)),
              threads(getEnvironmentVariable("SERVER_THREADS", Defaults::Server::SERVER_THREADS_)),
              debug_level(getEnvironmentVariable("SERVER_DEBUG_LEVEL", Defaults::Server::SERVER_DEBUG_LEVEL_)),
              log_to_console(getEnvironmentVariable("SERVER_LOG_TO_CONSOLE", Defaults::Server::SERVER_LOG_TO_CONSOLE_)),
              log_to_file(getEnvironmentVariable("SERVER_LOG_TO_FILE", Defaults::Server::SERVER_LOG_TO_FILE_)),
              log_dir(getEnvironmentVariable("SERVER_LOG_DIR", Defaults::Server::SERVER_LOG_DIR_)),
              log_file(getEnvironmentVariable("SERVER_LOG_FILE", Defaults::Server::SERVER_LOG_FILE_)),
              upload_dir(getEnvironmentVariable("SERVER_UPLOAD_DIR", Defaults::Server::SERVER_UPLOAD_DIR_))
        {
            optimize_performance(threads, 4);
        }

        void printValues() const override
        {
            Message::ConfMessage("------------------------------------------------");
            Message::ConfMessage("-------------------ServerConfig-----------------");
            Message::ConfMessage("------------------------------------------------");
            Message::ConfMessage(fmt::format("Host: {}", host));
            Message::ConfMessage(fmt::format("Port: {}", port));
            Message::ConfMessage(fmt::format("Name: {}", name));
            Message::ConfMessage(fmt::format("Version: {}", ver));
            Message::ConfMessage(fmt::format("Description: {}", desc));
            Message::ConfMessage(fmt::format("Threads: {}", threads));
            Message::ConfMessage(fmt::format("Debug Level: {}", debug_level));
            Message::ConfMessage(fmt::format("Log to console: {}", log_to_console));
            Message::ConfMessage(fmt::format("Log to file: {}", log_to_file));
            Message::ConfMessage(fmt::format("Log directory: {}", log_dir));
            Message::ConfMessage(fmt::format("Log file: {}", log_file));
            Message::ConfMessage(fmt::format("Upload directory: {}", upload_dir));
        }
    };

    using TokenManagerParameters = struct TokenManagerParameters : public EnvLoader
    {
        uint64_t    validity;
        std::string issuer;
        std::string type;
        std::string secret;

        TokenManagerParameters()
            : validity(getEnvironmentVariable("TOKEN_VALIDITY", Defaults::TokenParameters::TOKEN_VALIDITY_)),
              issuer(getEnvironmentVariable("TOKEN_ISSUER", Defaults::TokenParameters::TOKEN_ISSUER_)),
              type(getEnvironmentVariable("TOKEN_TYPE", Defaults::TokenParameters::TOKEN_TYPE_)),
              secret(getEnvironmentVariable("TOKEN_SECRET", Defaults::TokenParameters::TOKEN_SECRET_))
        {
        }

        void printValues() const override
        {
            Message::ConfMessage("------------------------------------------------");
            Message::ConfMessage("-------------TokenManagerParameters-------------");
            Message::ConfMessage("------------------------------------------------");
            Message::ConfMessage(fmt::format("Issuer: {}", issuer));
            Message::ConfMessage(fmt::format("Type: {}", type));
            Message::ConfMessage(fmt::format("Validity: {} minutes", validity));
            Message::ConfMessage(fmt::format("Secret: {}", secret));
        }
    };

    using FrontEndConfig = struct FrontEndConfig : public EnvLoader
    {
        uint16_t    port;
        std::string host;
        std::string invite_path;

        FrontEndConfig()
            : port(getEnvironmentVariable("FRONTEND_PORT", Defaults::FrontEnd::PORT_)),
              host(getEnvironmentVariable("FRONTEND_HOST", Defaults::FrontEnd::HOST_)),
              invite_path(getEnvironmentVariable("FRONTEND_INVITE_PATH", Defaults::FrontEnd::INVITE_PATH_))
        {
        }

        void printValues() const override
        {
            Message::ConfMessage("------------------------------------------------");
            Message::ConfMessage("------------------FrontEnd Config----------------");
            Message::ConfMessage("------------------------------------------------");
            Message::ConfMessage(fmt::format("Frontend host: {}", host));
            Message::ConfMessage(fmt::format("Frontend port: {}", port));
            Message::ConfMessage(fmt::format("Frontend invite path: {}", invite_path));
        }
    };

    using EmailSenderConfig = struct EmailSenderConfig : public EnvLoader
    {
        uint16_t    port;
        std::string host;
        std::string message_queue_path;

        EmailSenderConfig()
            : port(getEnvironmentVariable("EMAIL_SENDER_PORT", Defaults::EmailSenderDaemon::PORT_)),
              host(getEnvironmentVariable("EMAIL_SENDER_HOST", Defaults::EmailSenderDaemon::HOST_)),
              message_queue_path(getEnvironmentVariable("EMAIL_SENDER_QUEUE_PATH", Defaults::EmailSenderDaemon::QUEUE_PATH_))
        {
        }

        void printValues() const override
        {
            Message::ConfMessage("------------------------------------------------");
            Message::ConfMessage("----------------EmailSender Config--------------");
            Message::ConfMessage("------------------------------------------------");
            Message::ConfMessage(fmt::format("EmailSender host: {}", host));
            Message::ConfMessage(fmt::format("EmailSender port: {}", port));
            Message::ConfMessage(fmt::format("EmailSender queue path: {}", message_queue_path));
        }
    };

    using PermissionCacheConfig = struct PermissionCacheConfig : public EnvLoader
    {
        std::chrono::seconds ttl;  // idle lifetime of a cached permission; 0 disables the cache

        PermissionCacheConfig() : ttl(getEnvironmentVariable("PERMISSION_CACHE_TTL", std::chrono::seconds(Defaults::PermissionCache::TTL_))) {}

        void printValues() const override
        {
            Message::ConfMessage("------------------------------------------------");
            Message::ConfMessage("-------------Permission Cache Config------------");
            Message::ConfMessage("------------------------------------------------");
            Message::ConfMessage(fmt::format("Permission cache TTL: {} seconds", ttl.count()));
        }
    };

    using PasswordWorkerConfig = struct PasswordWorkerConfig : public EnvLoader
    {
        uint16_t workers;    // 0 runs password work on the request thread
        uint32_t queue_max;  // queued operations before a new one is refused with 503

        PasswordWorkerConfig()
            : workers(getEnvironmentVariable("PASSWORD_WORKERS", Defaults::PasswordWorker::WORKERS_)),
              queue_max(getEnvironmentVariable("PASSWORD_QUEUE_MAX", Defaults::PasswordWorker::QUEUE_MAX_))
        {
        }

        void printValues() const override
        {
            Message::ConfMessage("------------------------------------------------");
            Message::ConfMessage("-------------Password Worker Config-------------");
            Message::ConfMessage("------------------------------------------------");
            Message::ConfMessage(fmt::format("Password workers: {}", workers));
            Message::ConfMessage(fmt::format("Password queue max: {}", queue_max));
        }
    };

    using DbExecutorConfig = struct DbExecutorConfig : public EnvLoader
    {
        uint16_t workers;    // 0 runs database work on the request thread (tests)
        uint32_t queue_max;  // queued requests before a new one is refused with 503
        bool     io_fast_path;  // answer requests that need no database on the IO thread (DB_IO_FASTPATH)

        DbExecutorConfig()
            : workers(defaultWorkers()),
              queue_max(getEnvironmentVariable("DB_QUEUE_MAX", Defaults::DbExecutor::QUEUE_MAX_)),
              io_fast_path(getEnvironmentVariable("DB_IO_FASTPATH", Defaults::DbExecutor::IO_FAST_PATH_))
        {
            workers = getEnvironmentVariable("DB_WORKERS", workers);
        }

        void printValues() const override
        {
            Message::ConfMessage("------------------------------------------------");
            Message::ConfMessage("---------------DB Executor Config---------------");
            Message::ConfMessage("------------------------------------------------");
            Message::ConfMessage(fmt::format("DB workers: {}", workers));
            Message::ConfMessage(fmt::format("DB queue max: {}", queue_max));
            Message::ConfMessage(fmt::format("DB IO fast path: {}", io_fast_path));
        }

       private:
        // One worker per database connection, using the same pool size the connection pool will use.
        uint16_t defaultWorkers()
        {
            auto pool = static_cast<uint16_t>(getEnvironmentVariable("DB_MAX_CONN", static_cast<uint16_t>(Defaults::Database::DB_MAX_CONN_)));
            optimize_performance(pool, 5);
            return pool;
        }
    };

    using Testing = struct Testing : public EnvLoader
    {
       public:
        bool no_db; /*NOLINT */

        Testing() : no_db(getEnvironmentVariable("VALHALLA_TEST_NO_DB", Defaults::Testing::VALHALLA_TEST_NO_DB_)) {}

        void printValues() const override
        {
            Message::ConfMessage("------------------------------------------------");
            Message::ConfMessage("-----------------Testing Config-----------------");
            Message::ConfMessage("------------------------------------------------");
            Message::ConfMessage(fmt::format("Testing no_db: {}", no_db));
        }
    };

    // Template getter for structs

    template <Config T>
    T& get();

   private:
    ServerConfig           server_config_;
    DatabaseConfig         database_config_;
    DOSDetectorConfig      dos_detector_config_;
    TokenManagerParameters token_manager_parameters_;
    FrontEndConfig         frontend_config_;
    EmailSenderConfig      email_sender_config_;
    PermissionCacheConfig  permission_cache_config_;
    PasswordWorkerConfig   password_worker_config_;
    DbExecutorConfig       db_executor_config_;
    Testing                testing_;
};
