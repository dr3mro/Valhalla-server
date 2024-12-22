#pragma once

#include <memory>

// IWYU pragma: keep
#include "api/v2/filters/auth.hpp"
#include "api/v2/filters/ratelimit.hpp"
#include "api/v2/middlewares/elapsedtime.hpp"
#include "configurator/configurator.hpp"
#include "store/store.hpp"
#include "utils/Logger/logger.hpp"
#include "validator/databaseschema/databaseschema.hpp"
class Server
{
   public:
    Server()                         = default;
    virtual ~Server()                = default;
    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&)                 = default;
    Server& operator=(Server&&)      = delete;

    int run();

   private:
    std::shared_ptr<Logger>                            logger_       = Store::getObject<Logger>();
    std::shared_ptr<Configurator>                      configurator_ = Store::getObject<Configurator>();
    Configurator::ServerConfig                         config_       = configurator_->get<Configurator::ServerConfig>();
    Configurator::DatabaseConfig                       db_config_    = configurator_->get<Configurator::DatabaseConfig>();
    std::shared_ptr<DatabaseSchema>                    database      = Store::getObject<DatabaseSchema>();
    void                                               print_banner();
    std::shared_ptr<api::v2::Filters::Auth>            auth_filter_  = std::make_shared<api::v2::Filters::Auth>();
    std::shared_ptr<api::v2::MiddleWares::ElapsedTime> elapsed_time_ = std::make_shared<api::v2::MiddleWares::ElapsedTime>();
    std::shared_ptr<api::v2::Filters::RateLimit>       rate_limit_   = std::make_shared<api::v2::Filters::RateLimit>();
};
