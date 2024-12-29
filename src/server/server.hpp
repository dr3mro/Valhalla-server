#pragma once

#include <memory>

#include "api/v2/filters/auth.hpp"
#include "api/v2/filters/ratelimit.hpp"
#include "api/v2/middlewares/elapsedtime.hpp"
#include "configurator/configurator.hpp"

class Logger;
class DatabaseSchema;

class Server
{
   public:
    Server();

    virtual ~Server()                = default;
    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&)                 = default;
    Server& operator=(Server&&)      = delete;

    int run();

   private:
    void print_banner();

    std::shared_ptr<Logger>                            logger_;
    std::shared_ptr<Configurator>                      configurator_;
    Configurator::ServerConfig                         config_;
    Configurator::DatabaseConfig                       db_config_;
    std::shared_ptr<DatabaseSchema>                    databaseSchema_;
    std::shared_ptr<api::v2::Filters::Auth>            auth_filter_;
    std::shared_ptr<api::v2::MiddleWares::ElapsedTime> elapsed_time_;
    std::shared_ptr<api::v2::Filters::RateLimit>       rate_limit_;
};
