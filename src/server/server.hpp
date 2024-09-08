#pragma once
#include <crow.h>

#include <memory>

#include "routes/api_v1_routes.hpp"
#include "store/store.hpp"
class Server
{
   public:
    Server();
    ~Server() = default;

    int run();

   private:
    std::shared_ptr<Configurator>       configurator_ = Store::getObject<Configurator>();
    const Configurator::ServerConfig   &config_       = configurator_->get<Configurator::ServerConfig>();
    const Configurator::DatabaseConfig &db_config_    = configurator_->get<Configurator::DatabaseConfig>();
    std::shared_ptr<APP>                app;
    std::shared_ptr<API_V1_Routes>      routes;

    void print_banner();
};