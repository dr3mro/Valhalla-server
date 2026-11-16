#pragma once

#include <drogon/orm/DbClient.h>

#include <memory>
#include <string>

#include "configurator/configurator.hpp"

// Holds the Drogon PostgreSQL client used when DB_ASYNC=true. It is registered in the Store only when the flag is on;
// the synchronous pool stays in use while it is off. Tests replace it through Store::replaceObject.
class AsyncDbClient
{
   public:
    explicit AsyncDbClient(std::shared_ptr<drogon::orm::DbClient> client);
    AsyncDbClient(const AsyncDbClient&)            = delete;
    AsyncDbClient(AsyncDbClient&&)                 = delete;
    AsyncDbClient& operator=(const AsyncDbClient&) = delete;
    AsyncDbClient& operator=(AsyncDbClient&&)      = delete;
    virtual ~AsyncDbClient()                       = default;

    // A PostgreSQL client built from the DB_* settings, with DB_MAX_CONN connections.
    static std::shared_ptr<drogon::orm::DbClient> createClient();
    static std::string                            connectionString(const Configurator::DatabaseConfig& config);

    [[nodiscard]] const std::shared_ptr<drogon::orm::DbClient>& client() const { return client_; }

   private:
    std::shared_ptr<drogon::orm::DbClient> client_;
};
