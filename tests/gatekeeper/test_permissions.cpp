#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "entities/people/user.hpp"
#include "entities/people/provider.hpp"
#include "entities/services/laboratories.hpp"
#include "gatekeeper/permissionmanager/permissionmanager.hpp"
#include "gatekeeper/permissionmanager/permissionmanager_private.hpp"
#include "gatekeeper/permissionmanager/permissions.hpp"
#include "support/builders.hpp"

using Permissions      = ::Permissions;
using PowerLevel       = Permissions::PowerLevel;
using PermissionManagerPrivate = api::v2::PermissionManagerPrivate;
using PermissionManager        = api::v2::PermissionManager;
using Requester                = api::v2::Requester;
using HttpError                = api::v2::Http::Error;
namespace valhalla_test
{
    using jsoncons::json;

    inline std::optional<json> staffPermissions(const std::vector<std::pair<std::string, std::vector<std::string>>> &roles,
        const jsoncons::optional<uint64_t> ownerId = {}, const jsoncons::optional<uint64_t> adminId = {})
    {
        json staff;
        for (const auto &[role, members] : roles)
        {
            json membersJson = json::array();
            for (const auto &member : members)
            {
                membersJson.push_back(member);
            }
            staff[role] = membersJson;
        }
        json permissions;
        permissions["staff"] = staff;
        if (ownerId.has_value())
        {
            permissions["owner_id"] = ownerId.value();
        }
        if (adminId.has_value())
        {
            permissions["admin_id"] = adminId.value();
        }
        return std::optional<json>(permissions);
    }
}  // namespace valhalla_test

TEST_CASE("Permissions bitmask hasPower semantics", "[gatekeeper][permissions]")
{
    // NOTE: Permissions::hasPower() is not const-qualified, so objects cannot
    // be const (a const-correctness wart, not worth a refactor for tests).
    Permissions none(PowerLevel::NONE);
    CHECK_FALSE(none.hasPower(PowerLevel::CAN_READ));
    CHECK_FALSE(static_cast<bool>(none));

    Permissions reader(PowerLevel::CAN_READ);
    CHECK(reader.hasPower(PowerLevel::CAN_READ));
    CHECK_FALSE(reader.hasPower(PowerLevel::CAN_WRITE));
    CHECK(static_cast<bool>(reader));

    Permissions owner(PowerLevel::CAN_READ | PowerLevel::CAN_WRITE | PowerLevel::CAN_DELETE | PowerLevel::IS_ADMIN);
    CHECK(owner.hasPower(PowerLevel::CAN_READ));
    CHECK(owner.hasPower(PowerLevel::CAN_WRITE));
    CHECK(owner.hasPower(PowerLevel::CAN_DELETE));
    CHECK(owner.hasPower(PowerLevel::IS_ADMIN));
    CHECK_FALSE(owner.hasPower(PowerLevel::IS_SUPER));

    SECTION("operators")
    {
        auto level = PowerLevel::NONE;
        level |= PowerLevel::CAN_READ;
        CHECK(level == PowerLevel::CAN_READ);
        level &= PowerLevel::CAN_WRITE;
        CHECK(level == PowerLevel::NONE);
        CHECK((PowerLevel::CAN_READ & PowerLevel::CAN_READ) == PowerLevel::CAN_READ);
    }
}

TEST_CASE("PermissionManagerPrivate grants a staff member their configured power only", "[gatekeeper][permissions]")
{
    const auto requester = valhalla_test::makeRequester(7, "providers");
    const auto permissions_j = valhalla_test::staffPermissions({{"Doctors", {"7:4"}}});  // 4 = CAN_READ
    SECTION("CAN_READ is granted")
    {
        HttpError error;
        CHECK(PermissionManagerPrivate::hasPermission(requester, permissions_j, PowerLevel::CAN_READ, "clinics", error));
    }

    SECTION("CAN_WRITE and CAN_DELETE are denied")
    {
        HttpError error;
        CHECK_FALSE(PermissionManagerPrivate::hasPermission(requester, permissions_j, PowerLevel::CAN_WRITE, "clinics", error));

        HttpError deleteError;
        CHECK_FALSE(PermissionManagerPrivate::hasPermission(requester, permissions_j, PowerLevel::CAN_DELETE, "clinics", deleteError));
    }
}

TEST_CASE("PermissionManagerPrivate denies non-staff members", "[gatekeeper][permissions]")
{
    const auto requester = valhalla_test::makeRequester(99, "users");
    const auto permissions_j = valhalla_test::staffPermissions({{"Doctors", {"7:4"}}});

    HttpError error;
    CHECK_FALSE(PermissionManagerPrivate::hasPermission(requester, permissions_j, PowerLevel::CAN_READ, "clinics", error));
    CHECK(error.code == api::v2::Http::Status::FORBIDDEN);
}

TEST_CASE("PermissionManagerPrivate isStaffOfService parses the role string", "[gatekeeper][permissions]")
{
    const auto requester = valhalla_test::makeRequester(7, "users");
    auto permissions_j = valhalla_test::staffPermissions({{"Nurses", {"3:12", "7:32"}}});  // 32 = IS_ADMIN (0x20)

    SECTION("matching member")
    {
        HttpError error;
        const auto staff = PermissionManagerPrivate::isStaffOfService(requester, permissions_j, "clinics", error);
        REQUIRE(staff.has_value());
        CHECK(staff->staff_id == 7);
        CHECK(staff->power == PowerLevel::IS_ADMIN);  // 0x20
        CHECK(staff->isMember);
    }

    SECTION("member with a malformed power value is rejected")
    {
        permissions_j = valhalla_test::staffPermissions({{"Nurses", {"7:notanumber"}}});
        HttpError error;
        const auto staff = PermissionManagerPrivate::isStaffOfService(requester, permissions_j, "clinics", error);
        REQUIRE_FALSE(staff.has_value());
        CHECK(error.code == api::v2::Http::Status::FORBIDDEN);
    }

    SECTION("missing permissions payload is rejected")
    {
        HttpError error;
        const auto staff = PermissionManagerPrivate::isStaffOfService(requester, std::nullopt, "clinics", error);
        REQUIRE_FALSE(staff.has_value());
        CHECK(error.code == api::v2::Http::Status::BAD_REQUEST);
    }
}

TEST_CASE("PermissionManagerPrivate checks owner and admin of a service", "[gatekeeper][permissions]")
{
    const auto owner = valhalla_test::makeRequester(1, "providers");
    const auto admin = valhalla_test::makeRequester(2, "providers");
    const auto nobody = valhalla_test::makeRequester(3, "providers");
    const auto permissions_j = valhalla_test::staffPermissions({}, 1, 2);

    HttpError error;
    CHECK(PermissionManagerPrivate::isOwnerOfService(owner, permissions_j, "laboratories", error));
    CHECK_FALSE(PermissionManagerPrivate::isOwnerOfService(nobody, permissions_j, "laboratories", error));

    CHECK(PermissionManagerPrivate::isAdminOfService(admin, permissions_j, "laboratories", error));
    CHECK_FALSE(PermissionManagerPrivate::isAdminOfService(owner, permissions_j, "laboratories", error));
}

TEST_CASE("PermissionManagerPrivate isOwnerOrAdminOrHasPermission resolves role precedence", "[gatekeeper][permissions]")
{
    const auto permissions_j = valhalla_test::staffPermissions({{"Doctors", {"5:16"}}}, 1, 2);  // 16 = CAN_DELETE

    SECTION("owner is allowed")
    {
        HttpError error;
        CHECK(PermissionManagerPrivate::isOwnerOrAdminOrHasPermission(valhalla_test::makeRequester(1, "providers"), permissions_j, "clinics", error));
    }

    SECTION("admin is allowed")
    {
        HttpError error;
        CHECK(PermissionManagerPrivate::isOwnerOrAdminOrHasPermission(valhalla_test::makeRequester(2, "providers"), permissions_j, "clinics", error));
    }

    SECTION("staff with CAN_DELETE is allowed")
    {
        HttpError error;
        CHECK(PermissionManagerPrivate::isOwnerOrAdminOrHasPermission(valhalla_test::makeRequester(5, "users"), permissions_j, "clinics", error));
    }

    SECTION("staff without CAN_DELETE is denied")
    {
        // Known behaviour: with a permissions object that has no owner_id /
        // admin_id keys, isOwnerOfService()/isAdminOfService() dereference
        // jsoncons::operator[] on a missing key, which throws
        // "Key not found: 'owner_id'" out of the function (tests/README.md,
        // bug 11). With an owner/admin present, a CAN_READ-only member is
        // denied cleanly instead.
        const auto ownerOnly = valhalla_test::staffPermissions({{"Doctors", {"5:4"}}}, 1, 2);
        HttpError error;
        CHECK_FALSE(PermissionManagerPrivate::isOwnerOrAdminOrHasPermission(valhalla_test::makeRequester(5, "users"), ownerOnly, "clinics", error));
    }

    SECTION("permissions object without owner/admin keys throws out of the function")
    {
        // jsoncons const operator[] on a missing key throws; the exception
        // escapes isOwnerOrAdminOrHasPermission and error.code is left
        // untouched (uninitialised garbage in the caller's Http::Error).
        const auto staffOnly = valhalla_test::staffPermissions({{"Doctors", {"5:4"}}});
        HttpError error;
        REQUIRE_THROWS_AS(
            PermissionManagerPrivate::isOwnerOrAdminOrHasPermission(valhalla_test::makeRequester(5, "users"), staffOnly, "clinics", error), std::exception);
    }

    SECTION("no permissions at all is denied")
    {
        HttpError error;
        CHECK_FALSE(PermissionManagerPrivate::isOwnerOrAdminOrHasPermission(valhalla_test::makeRequester(5, "users"), std::nullopt, "clinics", error));
    }
}

TEST_CASE("PermissionManagerPrivate isOwnerOrAdmin requires either role", "[gatekeeper][permissions]")
{
    const auto permissions_j = valhalla_test::staffPermissions({}, 1, 2);
    HttpError error;

    CHECK(PermissionManagerPrivate::isOwnerOrAdmin(valhalla_test::makeRequester(1, "providers"), permissions_j, "clinics", error));
    CHECK(PermissionManagerPrivate::isOwnerOrAdmin(valhalla_test::makeRequester(2, "providers"), permissions_j, "clinics", error));
    CHECK_FALSE(PermissionManagerPrivate::isOwnerOrAdmin(valhalla_test::makeRequester(3, "providers"), permissions_j, "clinics", error));
}

TEST_CASE("PermissionManagerPrivate assert_group_id_match enforces same group and id", "[gatekeeper][permissions]")
{
    const auto requester = valhalla_test::makeRequester(11, "users");
    HttpError error;

    CHECK(PermissionManagerPrivate::assert_group_id_match(requester, "users", 11, error));

    SECTION("group mismatch")
    {
        CHECK_FALSE(PermissionManagerPrivate::assert_group_id_match(requester, "providers", 11, error));
        CHECK(error.code == api::v2::Http::Status::FORBIDDEN);
    }

    SECTION("id mismatch")
    {
        CHECK_FALSE(PermissionManagerPrivate::assert_group_id_match(requester, "users", 12, error));
        CHECK(error.code == api::v2::Http::Status::FORBIDDEN);
    }
}

TEST_CASE("PermissionManagerPrivate preServiceCreateChecks validates the creator", "[gatekeeper][permissions]")
{
    using valhalla_test::json;

    const auto makeService = [](jsoncons::optional<uint64_t> owner, jsoncons::optional<uint64_t> admin)
    {
        json service;
        if (owner.has_value())
        {
            service["owner_id"] = owner.value();
        }
        if (admin.has_value())
        {
            service["admin_id"] = admin.value();
        }
        return std::optional<json>(std::move(service));
    };

    SECTION("provider owning and administering the service is allowed")
    {
        const auto requester = valhalla_test::makeRequester(4, "providers");
        auto service_j = makeService(4, 4);
        HttpError error;
        CHECK(PermissionManagerPrivate::preServiceCreateChecks(requester, service_j, error));
    }

    SECTION("non-provider is rejected")
    {
        const auto requester = valhalla_test::makeRequester(4, "users");
        auto service_j = makeService(4, 4);
        HttpError error;
        CHECK_FALSE(PermissionManagerPrivate::preServiceCreateChecks(requester, service_j, error));
        CHECK(error.code == api::v2::Http::Status::BAD_REQUEST);
    }

    SECTION("owner/admin ids must match the requester")
    {
        const auto requester = valhalla_test::makeRequester(4, "providers");
        auto service_j = makeService(9, 4);
        HttpError error;
        CHECK_FALSE(PermissionManagerPrivate::preServiceCreateChecks(requester, service_j, error));
    }

    SECTION("missing ids are rejected")
    {
        const auto requester = valhalla_test::makeRequester(4, "providers");
        auto service_j = makeService(std::nullopt, std::nullopt);
        HttpError error;
        CHECK_FALSE(PermissionManagerPrivate::preServiceCreateChecks(requester, service_j, error));
    }

    SECTION("no payload at all is rejected")
    {
        HttpError error;
        CHECK_FALSE(PermissionManagerPrivate::preServiceCreateChecks(valhalla_test::makeRequester(4, "providers"), std::nullopt, error));
    }
}

TEST_CASE("PermissionManagerPrivate extract_json_value_safely returns values or a 400 error", "[gatekeeper][permissions]")
{
    using valhalla_test::json;

    json clinicJson;
    clinicJson["clinic_id"] = 3;
    std::optional<json> service_j(std::move(clinicJson));
    HttpError error;

    SECTION("present key")
    {
        const auto value = PermissionManagerPrivate::extract_json_value_safely<uint64_t>(service_j, "clinic_id", "clinic", error);
        REQUIRE(value.has_value());
        CHECK(value.value() == 3);
    }

    SECTION("missing key")
    {
        CHECK_FALSE(PermissionManagerPrivate::extract_json_value_safely<uint64_t>(service_j, "lab_id", "clinic", error).has_value());
        CHECK(error.code == api::v2::Http::Status::BAD_REQUEST);
    }

    SECTION("empty payload")
    {
        CHECK_FALSE(PermissionManagerPrivate::extract_json_value_safely<uint64_t>(std::nullopt, "clinic_id", "clinic", error).has_value());
    }

    SECTION("wrong type")
    {
        json wrongJson;
        wrongJson["clinic_id"] = "three";
        std::optional<json> wrongType(std::move(wrongJson));
        CHECK_FALSE(PermissionManagerPrivate::extract_json_value_safely<uint64_t>(wrongType, "clinic_id", "clinic", error).has_value());
    }
}

TEST_CASE("PermissionManager always allows creation for clients", "[gatekeeper][permissions]")
{
    const auto requester = valhalla_test::makeRequester(1, "users");
    HttpError error;
    CHECK(PermissionManager{}.canCreate<User>(requester, std::nullopt, error));
}

TEST_CASE("PermissionManager client paths require matching group and id", "[gatekeeper][permissions]")
{
    PermissionManager manager;

    SECTION("user reading own record")
    {
        HttpError error;
        CHECK(manager.canRead<User>(valhalla_test::makeRequester(5, "users"), 5, error));
    }

    SECTION("user reading another user's record")
    {
        HttpError error;
        CHECK_FALSE(manager.canRead<User>(valhalla_test::makeRequester(5, "users"), 6, error));
        CHECK(error.code == api::v2::Http::Status::FORBIDDEN);
    }

    SECTION("provider reading a provider record")
    {
        HttpError error;
        CHECK(manager.canRead<Provider>(valhalla_test::makeRequester(9, "providers"), 9, error));
    }

    SECTION("provider denied on another provider's id")
    {
        HttpError error;
        CHECK_FALSE(manager.canRead<Provider>(valhalla_test::makeRequester(9, "providers"), 10, error));
        CHECK(error.code == api::v2::Http::Status::FORBIDDEN);
    }
}

TEST_CASE("PermissionManager client paths agree across the whole CRUD matrix", "[gatekeeper][permissions]")
{
    PermissionManager manager;
    const auto        requester = valhalla_test::makeRequester(3, "users");

    SECTION("canRead/canUpdate/canDelete/canToggleActive/canGetServices for own id")
    {
        HttpError error;
        CHECK(manager.canRead<User>(requester, 3, error));
        CHECK(manager.canUpdate<User>(requester, 3, error));
        CHECK(manager.canDelete<User>(requester, 3, error));
        CHECK(manager.canToggleActive<User>(requester, 3, error));
        CHECK(manager.canGetServices<User>(requester, 3, error));
    }

    SECTION("all denied for another id")
    {
        HttpError error;
        CHECK_FALSE(manager.canRead<User>(requester, 4, error));
        CHECK_FALSE(manager.canUpdate<User>(requester, 4, error));
        CHECK_FALSE(manager.canDelete<User>(requester, 4, error));
        CHECK_FALSE(manager.canToggleActive<User>(requester, 4, error));
        CHECK_FALSE(manager.canGetServices<User>(requester, 4, error));
    }
}

TEST_CASE("PermissionManagerPrivate isOwnerOrAdminOrHasPermission checks the bit that matches the operation", "[gatekeeper][permissions]")
{
    // 4 = CAN_READ, 12 = CAN_READ|CAN_WRITE, 28 = CAN_READ|CAN_WRITE|CAN_DELETE
    const auto permissions_j = valhalla_test::staffPermissions({{"Readers", {"5:4"}}, {"Writers", {"6:12"}}, {"Deleters", {"7:28"}}}, 1, 2);

    const auto allowed = [&](const uint64_t id, const PowerLevel required)
    {
        HttpError error;
        return PermissionManagerPrivate::isOwnerOrAdminOrHasPermission(valhalla_test::makeRequester(id, "providers"), permissions_j, "clinics", error, required);
    };

    SECTION("a read-only member can read but not write or delete")
    {
        CHECK(allowed(5, PowerLevel::CAN_READ));
        CHECK_FALSE(allowed(5, PowerLevel::CAN_WRITE));
        CHECK_FALSE(allowed(5, PowerLevel::CAN_DELETE));
    }

    SECTION("a read/write member cannot delete")
    {
        CHECK(allowed(6, PowerLevel::CAN_READ));
        CHECK(allowed(6, PowerLevel::CAN_WRITE));
        CHECK_FALSE(allowed(6, PowerLevel::CAN_DELETE));
    }

    SECTION("a member with all three bits can do everything")
    {
        CHECK(allowed(7, PowerLevel::CAN_READ));
        CHECK(allowed(7, PowerLevel::CAN_WRITE));
        CHECK(allowed(7, PowerLevel::CAN_DELETE));
    }

    SECTION("owner and admin pass whatever bit is required")
    {
        CHECK(allowed(1, PowerLevel::CAN_DELETE));
        CHECK(allowed(2, PowerLevel::CAN_WRITE));
    }

    SECTION("someone who is not listed is denied")
    {
        CHECK_FALSE(allowed(99, PowerLevel::CAN_READ));
    }
}
