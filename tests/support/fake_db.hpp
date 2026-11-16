#pragma once

#include <jsoncons/json.hpp>
#include <memory>
#include <optional>
#include <pqxx/pqxx>
#include <string>
#include <unordered_set>

#include "controllers/databasecontroller/databasecontroller.hpp"
#include "drogon/utils/coroutine.h"
#include "database/databasebusy.hpp"
#include "store/store.hpp"
#include "utils/global/types.hpp"

/// Hermetic replacement for DatabaseController in unit tests.
///
/// Inject via FakeDbGuard (below) before constructing any controller that
/// captures databaseController from Store. All methods return the value set
/// in the corresponding public field (nullopt by default).
class FakeDatabaseController : public DatabaseController
{
   public:
    std::optional<jsoncons::json>                          query_result;
    std::optional<jsoncons::json>                          read_result;
    std::optional<jsoncons::json::array>                   search_result;
    std::optional<std::string>                             do_read_result;
    std::optional<bool>                                    exists_result;
    std::optional<jsoncons::json>                          password_result;
    std::optional<uint64_t>                                find_user_result;
    std::optional<std::unordered_set<api::v2::ColumnInfo>> schema_result;
    std::optional<std::unordered_set<std::string>>         tables_result;
    std::optional<jsoncons::json>                          permissions_result;

    /// When true every query throws DatabaseBusy, like a pool whose wait timed out.
    bool busy = false;

    /// How many times a write-capable query (executeQuery) ran, to check that a path did not write.
    int query_calls = 0;

    std::optional<jsoncons::json> executeQuery(const std::string&, const pqxx::params& = {}) override
    {
        throwIfBusy();
        ++query_calls;
        return query_result;
    }

    std::optional<jsoncons::json> executeReadQuery(const std::string&, const pqxx::params& = {}) override
    {
        throwIfBusy();
        return read_result;
    }

    std::optional<jsoncons::json::array> executeSearchQuery(const std::string&, const pqxx::params& = {}) override
    {
        throwIfBusy();
        return search_result;
    }

    std::optional<std::string> doReadQuery(const std::string&, const pqxx::params& = {}) override
    {
        throwIfBusy();
        return do_read_result;
    }

    std::optional<bool> checkItemExists(const std::string&, const std::string&, const std::string&) override
    {
        throwIfBusy();
        return exists_result;
    }

    std::optional<jsoncons::json> getPasswordHashForUserName(const std::string&, const std::string&) override
    {
        throwIfBusy();
        return password_result;
    }

    std::optional<std::unordered_set<api::v2::ColumnInfo>> getTableSchema(const std::string&) override { return schema_result; }

    std::optional<std::unordered_set<std::string>> getAllTables() override { return tables_result; }

    std::optional<jsoncons::json> getPermissions(const std::string&) override
    {
        throwIfBusy();
        return permissions_result;
    }

    // ---- Asynchronous (coroutine) seam for DB_ASYNC=true paths ----
    // The coroutine overrides answer from the same fields as their synchronous
    // counterparts, so one fake drives both paths hermetically.
    int coro_calls = 0;  // how many async queries ran, to check a path used the async seam

    drogon::Task<std::optional<jsoncons::json>> executeQueryCoro(const std::string&, const pqxx::params& = {}) override
    {
        throwIfBusy();
        ++query_calls;
        ++coro_calls;
        co_return query_result;
    }

    drogon::Task<std::optional<jsoncons::json>> executeReadQueryCoro(const std::string&, const pqxx::params& = {}) override
    {
        throwIfBusy();
        ++coro_calls;
        co_return read_result;
    }

    drogon::Task<std::optional<jsoncons::json::array>> executeSearchQueryCoro(const std::string&, const pqxx::params& = {}) override
    {
        throwIfBusy();
        ++coro_calls;
        co_return search_result;
    }

    drogon::Task<std::optional<std::string>> doReadQueryCoro(const std::string&, const pqxx::params& = {}) override
    {
        throwIfBusy();
        ++coro_calls;
        co_return do_read_result;
    }

    drogon::Task<std::optional<bool>> checkItemExistsCoro(const std::string&, const std::string&, const std::string&) override
    {
        throwIfBusy();
        ++coro_calls;
        co_return exists_result;
    }

    drogon::Task<std::optional<jsoncons::json>> getPasswordHashForUserNameCoro(const std::string&, const std::string&) override
    {
        throwIfBusy();
        ++coro_calls;
        co_return password_result;
    }

    drogon::Task<std::optional<jsoncons::json>> getPermissionsCoro(const std::string&) override
    {
        throwIfBusy();
        ++coro_calls;
        co_return permissions_result;
    }

   private:
    void throwIfBusy() const
    {
        if (busy)
        {
            throw DatabaseBusy();
        }
    }
};

/// RAII guard: injects a fake DatabaseController into the Store for the
/// lifetime of the guard, then restores the original on destruction.
///
/// Construct BEFORE creating any controller whose constructor calls
/// Store::getObject<DatabaseController>().
struct FakeDbGuard
{
    std::shared_ptr<DatabaseController> original;

    explicit FakeDbGuard(std::shared_ptr<FakeDatabaseController> fake)
    {
        original = Store::getObject<DatabaseController>();
        Store::replaceObject<DatabaseController>(std::move(fake));
    }

    ~FakeDbGuard() { Store::replaceObject<DatabaseController>(original); }

    FakeDbGuard(const FakeDbGuard&)            = delete;
    FakeDbGuard& operator=(const FakeDbGuard&) = delete;
};
