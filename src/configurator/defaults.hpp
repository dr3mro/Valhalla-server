#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
namespace Defaults
{
    namespace DosDetector
    {

        /*
         * Default values for DOSDetector configuration.
         */
        constexpr uint32_t MAX_REQUESTS_ = 10000;
        constexpr uint32_t PERIOD_       = 30;
        constexpr uint32_t MAX_FPS_      = 100;
        constexpr uint32_t RL_DURATION_  = 30;
        constexpr uint32_t BAN_DURATION_ = 3600;
        constexpr uint32_t CLN_FRQ_      = 30;
    };  // namespace DosDetector

    namespace Database
    {
        /*
         * Default values for Database configuration.
         */
        constexpr bool             DB_SSL_      = false;
        constexpr uint8_t          DB_MAX_CONN_ = 10;
        constexpr uint16_t         DB_PORT_     = 5432;
        constexpr std::string_view DB_HOST_     = std::string_view("172.20.0.2");
        constexpr std::string_view DB_NAME_     = std::string_view("postgres");
        constexpr std::string_view DB_USER_     = std::string_view("postgres");
        constexpr std::string_view DB_PASS_     = std::string_view("postgres");

    };  // namespace Database

    namespace Server
    {
        /*
         * Default values for Server configuration.
         */
        constexpr uint16_t         SERVER_THREADS_        = 4;
        constexpr uint16_t         SERVER_PORT_           = 8080;
        constexpr std::string_view SERVER_NAME_           = std::string_view("ProjectValhalla");
        constexpr std::string_view SERVER_VER_            = std::string_view("1.0.0");
        constexpr std::string_view SERVER_DESC_           = std::string_view("ProjectValhalla API");
        constexpr std::string_view SERVER_HOST_           = std::string_view("0.0.0.0");
        constexpr uint8_t          SERVER_DEBUG_LEVEL_    = 1;
        constexpr bool             SERVER_LOG_TO_CONSOLE_ = true;
        constexpr bool             SERVER_LOG_TO_FILE_    = true;
        constexpr std::string_view SERVER_LOG_DIR_        = std::string_view("./build/");
        constexpr std::string_view SERVER_LOG_FILE_       = std::string_view("server.log");

    }  // namespace Server

    namespace TokenParameters
    {
        constexpr uint64_t         TOKEN_VALIDITY_ = 43200;  // minutes
        constexpr std::string_view TOKEN_ISSUER_   = std::string_view("ProjectValhalla");
        constexpr std::string_view TOKEN_TYPE_     = std::string_view("JWS");
        constexpr std::string_view TOKEN_SECRET_   = std::string_view("01234567890123456789012345678901");
    }  // namespace TokenParameters

    namespace FrontEnd
    {
        constexpr std::string_view HOST_        = "http://172.20.0.5";
        constexpr uint16_t         PORT_        = 3000;
        constexpr std::string_view INVITE_PATH_ = "invite";

    }  // namespace FrontEnd

    namespace EmailSenderDaemon
    {
        constexpr std::string_view HOST_       = "http://172.20.0.6";
        constexpr uint16_t         PORT_       = 5000;
        constexpr std::string_view QUEUE_PATH_ = "/enqueue";
    }  // namespace EmailSenderDaemon
};  // namespace Defaults
