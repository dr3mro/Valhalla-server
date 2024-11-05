#pragma once

#include <fmt/core.h>
#include <fmt/ranges.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <type_traits>
#include <unordered_set>

#include "configurator/envloader/envloader.hpp"
#include "defaults.hpp"

template <typename T>
concept Config = std::is_base_of_v<EnvLoader, T>;

class Configurator
{
   public:
    Configurator()  = default;
    ~Configurator() = default;

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
            fmt::print("Max Requests: {}\n", max_requests);
            fmt::print("Period: {} seconds\n", period.count());
            fmt::print("Max FPS: {}\n", max_fingerprints);
            fmt::print("Rate Limit Duration: {} seconds\n", ratelimit_duration.count());
            fmt::print("Ban Duration: {} seconds\n", ban_duration.count());
            fmt::print("Cleanup Frequency: {} seconds\n", clean_freq);
            fmt::print("Whitelist: {}\n", fmt::join(whitelist, ", "));
            fmt::print("Blacklist: {}\n", fmt::join(blacklist, ", "));
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
            fmt::print("Host: {}\n", host);
            fmt::print("Port: {}\n", port);
            fmt::print("Name: {}\n", name);
            fmt::print("User: {}\n", user);
            fmt::print("Pass: {}\n", pass);
            fmt::print("SSL: {}\n", ssl);
            fmt::print("Max Connections: {}\n", max_conn);
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
        uint8_t          verbose_level;  // 0 Debug 1 info 2 warning 3 error 4 critical

        ServerConfig()
        {
            host          = getEnvironmentVariable(std::string_view("SERVER_HOST"), Defaults::Server::SERVER_HOST_);
            port          = getEnvironmentVariable(std::string_view("SERVER_PORT"), Defaults::Server::SERVER_PORT_);
            name          = getEnvironmentVariable(std::string_view("SERVER_NAME"), Defaults::Server::SERVER_NAME_);
            ver           = getEnvironmentVariable(std::string_view("SERVER_VERSION"), Defaults::Server::SERVER_VER_);
            desc          = getEnvironmentVariable(std::string_view("SERVER_DESCRIPTION"), Defaults::Server::SERVER_DESC_);
            threads       = getEnvironmentVariable(std::string_view("SERVER_THREADS"), Defaults::Server::SERVER_THREADS_);
            verbose_level = getEnvironmentVariable(std::string_view("SERVER_VERBOSE_LEVEL"), Defaults::Server::SERVER_VERBOSE_LEVEL_);

            optimize_performance(threads, 4);
        }

        void printValues() const override
        {
            fmt::print("Host: {}\n", host);
            fmt::print("Port: {}\n", port);
            fmt::print("Name: {}\n", name);
            fmt::print("Version: {}\n", ver);
            fmt::print("Description: {}\n", desc);
            fmt::print("Threads: {}\n", threads);
            fmt::print("Verbose Level: {}\n", verbose_level);
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
            fmt::print("Issuer: {}\n", issuer);
            fmt::print("Type: {}\n", type);
            fmt::print("Validity: {} minutes\n", validity);
            fmt::print("Secret: {}\n", secret);
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
            fmt::print("Frontend host: {}\n", host);
            fmt::print("Frontend port: {}\n", port);
            fmt::print("Frontend invite path: {}\n", invite_path);
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
            fmt::print("EmailSender host: {}\n", host);
            fmt::print("EmailSender port: {}\n", port);
            fmt::print("EmailSender queue path: {}\n", message_queue_path);
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
