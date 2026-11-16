#pragma once

#include <drogon/orm/DbClient.h>
#include <drogon/orm/SqlBinder.h>
#include <drogon/utils/coroutine.h>

#include <optional>
#include <pqxx/pqxx>
#include <string>
#include <vector>

// Adapter between the synchronous pqxx::params built by the entity SQL builders
// and Drogon's incremental SQL binder. The entity builders keep using
// pqxx::params; this module turns such a parameter list into the sequence of
// bind calls Drogon needs, so no entity code has to change for the async path.
namespace asyncdb
{
    // Plain-text view of the parameters, useful for the vector overload of
    // DbClient::execSqlCoro and for unit tests that cannot construct a client.
    [[nodiscard]] std::vector<std::optional<std::string>> toTextParams(const pqxx::params& params);

    // Bind every parameter in `params` to the already-created Drogon binder.
    void bindParams(drogon::orm::internal::SqlBinder& binder, const pqxx::params& params);

    // Run `sql` with `params` on `client` as a coroutine. The parameters are
    // bound in the same order the entity builder appended them.
    [[nodiscard]] drogon::orm::internal::SqlAwaiter execSqlCoro(drogon::orm::DbClient& client, const std::string& sql,
        const pqxx::params& params);
}  // namespace asyncdb

// Convenience alias matching the Phase 2 vocabulary.
namespace AsyncSqlBinder = asyncdb;
