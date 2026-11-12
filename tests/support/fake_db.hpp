#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

#include <jsoncons/json.hpp>
#include <pqxx/pqxx>

#include "controllers/databasecontroller/databasecontroller.hpp"
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

    std::optional<jsoncons::json> executeQuery(
        const std::string &, bool &, const pqxx::params & = {}, const std::string & = {}) override
    {
        return query_result;
    }

    std::optional<jsoncons::json> executeReadQuery(
        const std::string &, bool &, const pqxx::params & = {}, const std::string & = {}) override
    {
        return read_result;
    }

    std::optional<jsoncons::json::array> executeSearchQuery(
        const std::string &, bool &, const pqxx::params & = {}, const std::string & = {}) override
    {
        return search_result;
    }

    std::optional<std::string> doReadQuery(const std::string &, bool &) override
    {
        return do_read_result;
    }

    std::optional<bool> checkItemExists(
        const std::string &, const std::string &, const std::string &, bool &) override
    {
        return exists_result;
    }

    std::optional<jsoncons::json> getPasswordHashForUserName(
        const std::string &, const std::string &, bool &) override
    {
        return password_result;
    }

    std::optional<uint64_t> findIfUserID(const std::string &, const std::string &, bool &) override
    {
        return find_user_result;
    }

    std::optional<std::unordered_set<api::v2::ColumnInfo>> getTableSchema(
        const std::string &) override
    {
        return schema_result;
    }

    std::optional<std::unordered_set<std::string>> getAllTables() override
    {
        return tables_result;
    }

    std::optional<jsoncons::json> getPermissions(const std::string &, bool &) override
    {
        return permissions_result;
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

    FakeDbGuard(const FakeDbGuard &)            = delete;
    FakeDbGuard &operator=(const FakeDbGuard &) = delete;
};
