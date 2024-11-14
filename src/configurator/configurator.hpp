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
#include "fmt/color.h"

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

    using DOSDetectorConfig = struct DODDetectorConfig : public EnvLoader
    {
        uint32_t                        max_requests;
        uint32_t                        clean_freq;
        uint32_t                        max_fingerprints;
        std::chrono::seconds            period;
        std::chrono::seconds            ratelimit_duration;
        std::chrono::seconds            ban_duration;
        std::unordered_set<std::string> whitelist;
        std::unordered_set<std::string> blacklist;

        DODDetectorConfig()
        {
            max_requests       = getEnvironmentVariable("MAX_REQUESTS", Defaults::DosDetector::MAX_REQUESTS_);
            period             = getEnvironmentVariable("PERIOD", std::chrono::seconds(Defaults::DosDetector::PERIOD_));
            max_fingerprints   = getEnvironmentVariable("MAX_FPS", Defaults::DosDetector::MAX_FPS_);
            ratelimit_duration = getEnvironmentVariable("RL_DURATION", std::chrono::seconds(Defaults::DosDetector::RL_DURATION_));
            ban_duration       = getEnvironmentVariable("BAN_DURATION", std::chrono::seconds(Defaults::DosDetector::BAN_DURATION_));
            clean_freq         = getEnvironmentVariable("CLN_FRQ", Defaults::DosDetector::CLN_FRQ_);
            whitelist          = getEnvironmentVariable("WHITELIST");
            blacklist          = getEnvironmentVariable("BLACKLIST");
        }

        void printValues() const override
        {
            print_config(fmt::format("Max Requests: {}", max_requests));
            print_config(fmt::format("Period: {} seconds", period.count()));
            print_config(fmt::format("Max FPS: {}", max_fingerprints));
            print_config(fmt::format("Rate Limit Duration: {} seconds", ratelimit_duration.count()));
            print_config(fmt::format("Ban Duration: {} seconds", ban_duration.count()));
            print_config(fmt::format("Cleanup Frequency: {} seconds", clean_freq));
            print_config(fmt::format("Whitelist: {}", fmt::join(whitelist, ", ")));
            print_config(fmt::format("Blacklist: {}", fmt::join(blacklist, ", ")));
        }
    };

    using DatabaseConfig = struct DatabaseConfig : public EnvLoader
    {
        bool             ssl;
        uint16_t         max_conn;
        uint16_t         port;
        std::string_view name;
        std::string_view user;
        std::string_view pass;
        std::string_view host;

        DatabaseConfig()
        {
            host     = getEnvironmentVariable(std::string_view("DB_HOST"), Defaults::Database::DB_HOST_);
            port     = getEnvironmentVariable(std::string_view("DB_PORT"), Defaults::Database::DB_PORT_);
            name     = getEnvironmentVariable(std::string_view("DB_NAME"), Defaults::Database::DB_NAME_);
            user     = getEnvironmentVariable(std::string_view("DB_USER"), Defaults::Database::DB_USER_);
            pass     = getEnvironmentVariable(std::string_view("DB_PASS"), Defaults::Database::DB_PASS_);
            ssl      = getEnvironmentVariable(std::string_view("DB_SSL"), Defaults::Database::DB_SSL_);
            max_conn = getEnvironmentVariable(std::string_view("DB_MAX_CONN"), Defaults::Database::DB_MAX_CONN_);

            optimize_performance(max_conn, 5);
        }

        void printValues() const override
        {
            print_config(fmt::format("Host: {}", host));
            print_config(fmt::format("Port: {}", port));
            print_config(fmt::format("Name: {}", name));
            print_config(fmt::format("User: {}", user));
            print_config(fmt::format("Pass: {}", pass));
            print_config(fmt::format("SSL: {}", ssl));
            print_config(fmt::format("Max Connections: {}", max_conn));
        }
    };

    using ServerConfig = struct ServerConfig : public EnvLoader
    {
        std::string_view host;
        std::string_view name;
        std::string_view ver;
        std::string_view desc;
        uint16_t         port;
        uint16_t         threads;
        uint8_t          debug_level;  // 0 Debug 1 info 2 warning 3 error 4 critical
        bool             log_to_console;
        bool             log_to_file;
        std::string_view log_dir;
        std::string_view log_file;

        ServerConfig()
        {
            host           = getEnvironmentVariable(std::string_view("SERVER_HOST"), Defaults::Server::SERVER_HOST_);
            port           = getEnvironmentVariable(std::string_view("SERVER_PORT"), Defaults::Server::SERVER_PORT_);
            name           = getEnvironmentVariable(std::string_view("SERVER_NAME"), Defaults::Server::SERVER_NAME_);
            ver            = getEnvironmentVariable(std::string_view("SERVER_VERSION"), Defaults::Server::SERVER_VER_);
            desc           = getEnvironmentVariable(std::string_view("SERVER_DESCRIPTION"), Defaults::Server::SERVER_DESC_);
            threads        = getEnvironmentVariable(std::string_view("SERVER_THREADS"), Defaults::Server::SERVER_THREADS_);
            debug_level    = getEnvironmentVariable(std::string_view("SERVER_DEBUG_LEVEL"), Defaults::Server::SERVER_DEBUG_LEVEL_);
            log_to_console = getEnvironmentVariable(std::string_view("SERVER_LOG_TO_CONSOLE"), Defaults::Server::SERVER_LOG_TO_CONSOLE_);
            log_to_file    = getEnvironmentVariable(std::string_view("SERVER_LOG_TO_FILE"), Defaults::Server::SERVER_LOG_TO_FILE_);
            log_dir        = getEnvironmentVariable(std::string_view("SERVER_LOG_DIR"), Defaults::Server::SERVER_LOG_DIR_);
            log_file       = getEnvironmentVariable(std::string_view("SERVER_LOG_FILE"), Defaults::Server::SERVER_LOG_FILE_);

            optimize_performance(threads, 4);
        }

        void printValues() const override
        {
            print_config(fmt::format("Host: {}", host));
            print_config(fmt::format("Port: {}", port));
            print_config(fmt::format("Name: {}", name));
            print_config(fmt::format("Version: {}", ver));
            print_config(fmt::format("Description: {}", desc));
            print_config(fmt::format("Threads: {}", threads));
            print_config(fmt::format("Debug Level: {}", debug_level));
            print_config(fmt::format("Log to console: {}", log_to_console));
            print_config(fmt::format("Log to file: {}", log_to_file));
            print_config(fmt::format("Log directory: {}", log_dir));
            print_config(fmt::format("Log file: {}", log_file));
        }
    };

    using TokenManagerParameters = struct TokenManagerParameters : public EnvLoader
    {
        uint64_t         validity;
        std::string_view issuer;
        std::string_view type;
        std::string_view secret;

        TokenManagerParameters()
        {
            issuer   = getEnvironmentVariable(std::string_view("TOKEN_ISSUER"), Defaults::TokenParameters::TOKEN_ISSUER_);
            type     = getEnvironmentVariable(std::string_view("TOKEN_TYPE"), Defaults::TokenParameters::TOKEN_TYPE_);
            validity = getEnvironmentVariable(std::string_view("TOKEN_VALIDITY"), Defaults::TokenParameters::TOKEN_VALIDITY_);
            secret   = getEnvironmentVariable(std::string_view("TOKEN_SECRET"), Defaults::TokenParameters::TOKEN_SECRET_);
        }

        void printValues() const override
        {
            print_config(fmt::format("Issuer: {}", issuer));
            print_config(fmt::format("Type: {}", type));
            print_config(fmt::format("Validity: {} minutes", validity));
            print_config(fmt::format("Secret: {}", secret));
        }
    };

    using FrontEndConfig = struct FrontEndConfig : public EnvLoader
    {
        uint16_t         port;
        std::string_view host;
        std::string_view invite_path;

        FrontEndConfig()
        {
            host        = getEnvironmentVariable(std::string_view("FRONTEND_HOST"), Defaults::FrontEnd::HOST_);
            port        = getEnvironmentVariable(std::string_view("FRONTEND_PORT"), Defaults::FrontEnd::PORT_);
            invite_path = getEnvironmentVariable(std::string_view("FRONTEND_INVITE_PATH"), Defaults::FrontEnd::INVITE_PATH_);
        }

        void printValues() const override
        {
            print_config(fmt::format("Frontend host: {}", host));
            print_config(fmt::format("Frontend port: {}", port));
            print_config(fmt::format("Frontend invite path: {}", invite_path));
        }
    };

    using EmailSenderConfig = struct EmailSenderConfig : public EnvLoader
    {
        uint16_t         port;
        std::string_view host;
        std::string_view message_queue_path;

        EmailSenderConfig()
        {
            host               = getEnvironmentVariable(std::string_view("EMAIL_SENDER_HOST"), Defaults::EmailSenderDaemon::HOST_);
            port               = getEnvironmentVariable(std::string_view("EMAIL_SENDER_PORT"), Defaults::EmailSenderDaemon::PORT_);
            message_queue_path = getEnvironmentVariable(std::string_view("EMAIL_SENDER_QUEUE_PATH"), Defaults::EmailSenderDaemon::QUEUE_PATH_);
        }

        void printValues() const override
        {
            print_config(fmt::format("EmailSender host: {}", host));
            print_config(fmt::format("EmailSender port: {}", port));
            print_config(fmt::format("EmailSender queue path: {}", message_queue_path));
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

    static void print_config(const std::string& conf)
    {
        fmt::print("{}{}{}{}{}\n", fmt::format(fmt::fg(fmt::color::indian_red), "[Project Valhalla]"), fmt::format(fmt::fg(fmt::color::white), " ["),
                   fmt::format(fmt::fg(fmt::color::green), "config"), fmt::format(fmt::fg(fmt::color::white), "] "), conf);
    }
};
