#pragma once

#include <cstdint>
#include <string>

namespace Defaults
{
    namespace DosDetector
    {

        /*
         * Default values for DOSDetector configuration.
         */
        const uint32_t MAX_REQUESTS_ = 10000;
        const uint32_t PERIOD_       = 30;
        const uint32_t MAX_FPS_      = 100;
        const uint32_t RL_DURATION_  = 30;
        const uint32_t BAN_DURATION_ = 3600;
        const uint32_t CLN_FRQ_      = 30;
    };  // namespace DosDetector

    namespace Database
    {
        /*
         * Default values for Database configuration.
         */
        const bool        DB_SSL_      = false;
        const uint8_t     DB_MAX_CONN_ = 10;
        const uint16_t    DB_PORT_     = 5432;
        const std::string DB_HOST_     = "172.20.0.2";
        const std::string DB_NAME_     = "postgres";
        const std::string DB_USER_     = "postgres";
        const std::string DB_PASS_     = "postgres";

    };  // namespace Database

    namespace Server
    {
        /*
         * Default values for Server configuration.
         */
        const uint16_t    SERVER_THREADS_        = 4;
        const uint16_t    SERVER_PORT_           = 8080;
        const std::string SERVER_NAME_           = "ProjectValhalla";
        const std::string SERVER_VER_            = "1.0.0";
        const std::string SERVER_DESC_           = "ProjectValhalla API";
        const std::string SERVER_HOST_           = "0.0.0.0";
        const uint8_t     SERVER_DEBUG_LEVEL_    = 1;
        const bool        SERVER_LOG_TO_CONSOLE_ = true;
        const bool        SERVER_LOG_TO_FILE_    = true;
        const std::string SERVER_LOG_DIR_        = "./logs/";
        const std::string SERVER_LOG_FILE_       = "server.log";
        const std::string SERVER_UPLOAD_DIR_     = "./uploads/";

    }  // namespace Server

    namespace TokenParameters
    {
        const uint64_t    TOKEN_VALIDITY_ = 43200;  // minutes
        const std::string TOKEN_ISSUER_   = "ProjectValhalla";
        const std::string TOKEN_TYPE_     = "JWS";
        const std::string TOKEN_SECRET_   = "01234567890123456789012345678901";
    }  // namespace TokenParameters

    namespace FrontEnd
    {
        const std::string HOST_        = "http://172.20.0.5";
        const uint16_t    PORT_        = 3000;
        const std::string INVITE_PATH_ = "invite";

    }  // namespace FrontEnd

    namespace EmailSenderDaemon
    {
        const std::string HOST_       = "http://172.20.0.6";
        const uint16_t    PORT_       = 5000;
        const std::string QUEUE_PATH_ = "/enqueue";
    }  // namespace EmailSenderDaemon
};  // namespace Defaults
