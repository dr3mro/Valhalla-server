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
    auto        client = drogon::orm::DbClient::newPgClient(connectionString(config), config.max_conn);
    // Bound how long a request may sit waiting for a connection from the pool; the synchronous
    // pool uses the same DB_ACQUIRE_TIMEOUT_MS value. Drogon takes seconds.
    client->setTimeout(static_cast<double>(config.acquire_timeout_ms) / 1000.0);
    return client;
}

std::string AsyncDbClient::connectionString(const Configurator::DatabaseConfig& config)
{
    // The container image overrides DB_HOST and DB_SSL; both flow through the same DatabaseConfig.
    return fmt::format("host={} port={} dbname={} user={} password={} connect_timeout=2 sslmode={}", config.host, config.port, config.name, config.user,
        config.pass, config.ssl ? "require" : "disable");
}
