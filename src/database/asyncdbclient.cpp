#include "database/asyncdbclient.hpp"

#include <drogon/orm/DbClient.h>
#include <fmt/format.h>

#include <memory>
#include <string>
#include <utility>

#include "configurator/configurator.hpp"
#include "store/store.hpp"

AsyncDbClient::AsyncDbClient(std::shared_ptr<drogon::orm::DbClient> client) : client_(std::move(client)) {}

std::shared_ptr<drogon::orm::DbClient> AsyncDbClient::createClient()
{
    const auto& config = Store::getObject<Configurator>()->get<Configurator::DatabaseConfig>();
    return drogon::orm::DbClient::newPgClient(connectionString(config), config.max_conn);
}

std::string AsyncDbClient::connectionString(const Configurator::DatabaseConfig& config)
{
    return fmt::format("host={} port={} dbname={} user={} password={} connect_timeout=2", config.host, config.port, config.name, config.user, config.pass);
}
