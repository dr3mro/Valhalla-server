#pragma once

#include <fmt/core.h>
#include <fmt/ranges.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <type_traits>
#include <unordered_set>

#include "configurator/envloader/envloader.hpp"
#include "defaults.hpp"
#include "utils/message/message.hpp"

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
        uint16_t    max_conn;
        uint16_t    port;
        std::string name;
        std::string user;
        std::string pass;
        std::string host;

        DatabaseConfig()
            : ssl(getEnvironmentVariable("DB_SSL", Defaults::Database::DB_SSL_)),
              max_conn(getEnvironmentVariable("DB_MAX_CONN", Defaults::Database::DB_MAX_CONN_)),
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
            Message::ConfMessage(fmt::format("Max Connections: {}", max_conn));
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
              ver(getEnvironmentVariable("SERVER_VERSION", Defaults::Server::SERVER_VER_)),
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
};
