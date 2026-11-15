#pragma once

#include <jsoncons/json.hpp>
#include <memory>
#include <optional>
#include <pqxx/pqxx>
#include <string>
#include <unordered_set>

#include "controllers/databasecontroller/databasecontroller.hpp"
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
