#pragma once
#include <drogon/drogon.h>

#include <memory>

#include "configurator/configurator.hpp"
#include "routes/v2_routes.hpp"  // IWYU pragma: keep
#include "store/store.hpp"

class Server2
{
   public:
    Server2()          = default;
    virtual ~Server2() = default;

    int run();

   private:
    std::shared_ptr<Configurator>       configurator_ = Store::getObject<Configurator>();
    const Configurator::ServerConfig   &config_       = configurator_->get<Configurator::ServerConfig>();
    const Configurator::DatabaseConfig &db_config_    = configurator_->get<Configurator::DatabaseConfig>();
    void                                print_banner();
};
