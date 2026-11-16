/**
 * @file test_permissions.cpp
 * @brief Catch2 unit tests and fixtures for permissions. Checks the power
 * bitmask and the helpers that decide who may read, create or change a client
 * or service record.
 * @details Permissions wraps a power bitmask (CAN_READ, CAN_WRITE, CAN_DELETE,
 * IS_ADMIN, IS_SUPER); the tests pin down hasPower and the bitwise operators.
 * PermissionManagerPrivate resolves roles from a JSON permissions document -
 * staff encoded as "id:power" strings plus owner/admin ids - and extracts
 * values safely. The public PermissionManager facade is then checked across a
 * CRUD matrix for both clients and services. Several sections deliberately
 * record current behaviour, such as a missing owner_id key escaping as an
 * exception rather than becoming a clean error.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It protects permissions.hpp and the permissionmanager headers, the
 * authorization layer that decides whether a requester may act on a record. A
 * slip here could let a client read someone else's medical data or stop a
 * doctor doing their job, and bitmasks plus JSON parsing are exactly where
 * off-by-one and type bugs hide. Pinning down the intended rules and the
 * documented quirks prevents silent changes to access control.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE / SECTION (sections re-run the body with fresh setup):
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Catch2 CHECK / REQUIRE / REQUIRE_FALSE / REQUIRE_THROWS_AS macros:
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - std::optional, used for values that may be absent (owner id, permissions
 * payload): https://en.cppreference.com/w/cpp/utility/optional
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Provides TEST_CASE, SECTION and the CHECK/REQUIRE family of macros.
#include <catch2/catch_test_macros.hpp>
// Provides data-driven helpers (included for consistency across the suite).
#include <catch2/generators/catch_generators.hpp>
#include <cstdint>   // uint64_t, the type of client, owner and admin ids.
#include <optional>  // std::optional, used throughout for possibly-absent data.
#include <string>    // std::string for role names and group names.
#include <vector>    // std::vector, used by the permissions-document builder.

// Provider entity type used to select the provider permission path.
#include "entities/people/provider.hpp"
// User entity type used to select the client permission path.
#include "entities/people/user.hpp"
// Laboratories entity used in service-creation tests.
#include "entities/services/laboratories.hpp"
// The public facade under test.
#include "gatekeeper/permissionmanager/permissionmanager.hpp"
// The internal role-resolving helpers under test.
#include "gatekeeper/permissionmanager/permissionmanager_private.hpp"
// The power bitmask type under test.
#include "gatekeeper/permissionmanager/permissions.hpp"
#include "support/builders.hpp"  // makeRequester and other test-data builders.

using Permissions =
    ::Permissions;  // Short alias for the global Permissions type.
using PowerLevel =
    Permissions::PowerLevel;  // Short alias for the bitmask enum.
using PermissionManagerPrivate =
    api::v2::PermissionManagerPrivate;  // Alias for the internal helper class.
using PermissionManager =
    api::v2::PermissionManager;        // Alias for the public facade class.
using Requester = api::v2::Requester;  // Alias for the "who is asking" type.
using HttpError =
    api::v2::Http::Error;  // Alias for the error object helpers fill on denial.
namespace valhalla_test {
  using jsoncons::json;  // Short alias for the JSON type used to build
                         // permission documents.

  /** @brief Builds the permissions JSON consumed by the permission helpers.
   *
   * Each role maps to "members" encoded as "<staff_id>:<power>" strings,
   * where <power> is the decimal bitmask (e.g. 4 = CAN_READ, 16 =
   * CAN_DELETE). owner_id/admin_id are emitted only when supplied, which is
   * significant: some helpers read those keys unconditionally (see the
   * throwing SECTION in the role-precedence test below).
   */
  // Role names paired with their member strings, e.g.
  // {"Doctors", {"7:4"}}.
  // Optional owner/admin ids; omitting them deliberately leaves
  // those JSON keys out.
  inline std::optional<json> staffPermissions(
      const std::vector<std::pair<std::string, std::vector<std::string>>>&
                                         roles,
      const jsoncons::optional<uint64_t> ownerId = {},
      const jsoncons::optional<uint64_t> adminId = {}) {
    json staff;  // Will become the "staff" object holding roles.
    for (const auto& [role, members] :
         // Loop over each role; structured bindings unpack the pair.
         roles) {
      json membersJson = json::array();  // Build a JSON array for this
                                         // role's member strings.
      for (const auto& member :
           // Loop over each "id:power" string in the role.
           members) {
        membersJson.push_back(
            member);  // Append the member string to the role's array.
      }
      staff[role] =
          membersJson;  // Attach the finished array under the role name.
    }
    json permissions;              // The top-level permission document.
    permissions["staff"] = staff;  // Put all the roles under the "staff"
                                   // key the helpers expect.
    // Only write owner_id when the caller
    // supplied one; this omission is intentional
    // in some tests.
    if (ownerId.has_value()) {
      permissions["owner_id"] =
          ownerId.value();  // Store the owner's client id.
    }
    // Same conditional treatment for the admin id.
    if (adminId.has_value()) {
      permissions["admin_id"] =
          adminId.value();  // Store the admin's client id.
    }
    return std::optional<json>(
        permissions);  // Return the document wrapped in optional so "no
                       // permissions" is also representable.
  }
}  // namespace valhalla_test

// Registers the test for the power
// bitmask type itself.
TEST_CASE("Permissions bitmask hasPower semantics",
          "[gatekeeper][permissions]") {
  // NOTE: Permissions::hasPower() is not const-qualified, so objects cannot
  // be const (a const-correctness wart, not worth a refactor for tests).
  Permissions none(
      PowerLevel::NONE);  // A permissions object with no powers at all.
  CHECK_FALSE(none.hasPower(PowerLevel::CAN_READ));  // Prove it grants nothing.
  CHECK_FALSE(static_cast<bool>(
      none));  // Prove it converts to false, i.e. "no powers".

  Permissions reader(
      PowerLevel::CAN_READ);  // An object carrying just the read bit.
  CHECK(reader.hasPower(PowerLevel::CAN_READ));  // Prove it grants reading.
  CHECK_FALSE(reader.hasPower(
      PowerLevel::CAN_WRITE));  // Prove it does not grant writing.
  CHECK(
      static_cast<bool>(reader));  // Prove any nonzero power converts to true.

  Permissions owner(PowerLevel::CAN_READ | PowerLevel::CAN_WRITE |
                    PowerLevel::CAN_DELETE |
                    PowerLevel::IS_ADMIN);  // Combine four bits with OR; each
                                            // bit is a separate capability.
  CHECK(owner.hasPower(
      PowerLevel::CAN_READ));  // Prove the read bit survives the combination.
  CHECK(
      owner.hasPower(PowerLevel::CAN_WRITE));  // Prove the write bit survives.
  CHECK(owner.hasPower(
      PowerLevel::CAN_DELETE));  // Prove the delete bit survives.
  CHECK(owner.hasPower(PowerLevel::IS_ADMIN));  // Prove the admin bit survives.
  CHECK_FALSE(owner.hasPower(
      PowerLevel::IS_SUPER));  // Prove an unlisted bit is not granted just
                               // because others are.

  SECTION("operators") {
    auto level =
        PowerLevel::NONE;  // An empty bitmask to modify with operators.
    level |= PowerLevel::CAN_READ;  // Add the read bit using OR-assignment.
    CHECK(level == PowerLevel::CAN_READ);  // Prove the bit was added.
    level &= PowerLevel::CAN_WRITE;        // Keep only bits also present in
                                           // CAN_WRITE using AND-assignment.
    CHECK(level == PowerLevel::NONE);      // Prove read and write are distinct
                                           // bits, so the result is empty.
    CHECK((PowerLevel::CAN_READ & PowerLevel::CAN_READ) ==
          PowerLevel::CAN_READ);  // Prove AND of a bit with itself returns
                                  // that bit; parentheses help Catch2 parse
                                  // the expression.
  }
}

TEST_CASE(
    "PermissionManagerPrivate grants a staff member their configured power "
    // Registers the test that staff get exactly
    // the power listed for them.
    "only",
    "[gatekeeper][permissions]") {
  const auto requester =
      valhalla_test::makeRequester(7, "providers");  // A provider with client
                                                     // id 7.
  const auto permissions_j = valhalla_test::staffPermissions(
      {{"Doctors",
        {"7:4"}}});  // 4 = CAN_READ -- list id 7 with only read power.
  SECTION("CAN_READ is granted") {
    HttpError error;  // Receives an error description on denial; should
                      // stay unused here.
    CHECK(PermissionManagerPrivate::hasPermission(requester,
                                                  permissions_j,
                                                  PowerLevel::CAN_READ,
                                                  "clinics",
                                                  error));  // Prove the
                                                            // configured read
                                                            // power is granted.
  }

  SECTION("CAN_WRITE and CAN_DELETE are denied") {
    HttpError error;  // Receives the denial reason for the write attempt.
    CHECK_FALSE(
        PermissionManagerPrivate::hasPermission(requester,
                                                permissions_j,
                                                PowerLevel::CAN_WRITE,
                                                "clinics",
                                                error));  // Prove write is
                                                          // refused even though
                                                          // the member exists.

    HttpError deleteError;  // Separate error object so the two checks do
                            // not interfere.
    CHECK_FALSE(
        PermissionManagerPrivate::
            hasPermission(requester,
                          permissions_j,
                          PowerLevel::CAN_DELETE,
                          "clinics",
                          deleteError));  // Prove delete is refused too.
  }
}

// Registers the test that someone absent from
// the staff list is rejected.
TEST_CASE("PermissionManagerPrivate denies non-staff members",
          "[gatekeeper][permissions]") {
  const auto requester =
      valhalla_test::makeRequester(99, "users");  // Client 99, not present in
                                                  // any role.
  const auto permissions_j = valhalla_test::staffPermissions(
      {{"Doctors", {"7:4"}}});  // Only staff id 7 is listed.

  HttpError error;  // Receives the denial reason.
  CHECK_FALSE(
      PermissionManagerPrivate::hasPermission(requester,
                                              permissions_j,
                                              PowerLevel::CAN_READ,
                                              "clinics",
                                              error));  // Prove a non-member
                                                        // cannot read.
  CHECK(error.code ==
        api::v2::Http::Status::FORBIDDEN);  // Prove the denial is reported as
                                            // HTTP 403 Forbidden.
}

// Registers the test for parsing
// "id:power" strings and error cases.
TEST_CASE("PermissionManagerPrivate isStaffOfService parses the role string",
          "[gatekeeper][permissions]") {
  const auto requester =
      valhalla_test::makeRequester(7,
                                   "users");  // Client id 7 will be looked up.
  auto permissions_j = valhalla_test::staffPermissions(
      {{"Nurses", {"3:12", "7:32"}}});  // 32 = IS_ADMIN (0x20) -- two members
                                        // with different powers.

  SECTION("matching member") {
    HttpError  error;  // Receives any parse error.
    const auto staff =
        PermissionManagerPrivate::isStaffOfService(requester,
                                                   permissions_j,
                                                   "clinics",
                                                   error);  // Look up client 7
                                                            // among the nurses.
    REQUIRE(staff.has_value());   // Prove a matching member record was found
                                  // before reading it.
    CHECK(staff->staff_id == 7);  // Prove the id was parsed correctly.
    CHECK(staff->power ==
          PowerLevel::IS_ADMIN);  // 0x20 -- proves the decimal power 32
                                  // became the IS_ADMIN bit.
    CHECK(staff->isMember);       // Prove the result is flagged as an actual
                                  // member.
  }

  SECTION("member with a malformed power value is rejected") {
    permissions_j = valhalla_test::staffPermissions(
        {{"Nurses",
          {"7:notanumber"}}});  // Replace with an unparseable power string.
    HttpError  error;           // Receives the error.
    const auto staff =
        PermissionManagerPrivate::isStaffOfService(requester,
                                                   permissions_j,
                                                   "clinics",
                                                   error);  // Try to parse the
                                                            // bad entry.
    REQUIRE_FALSE(staff.has_value());  // Prove no staff record is produced.
    CHECK(error.code ==
          api::v2::Http::Status::FORBIDDEN);  // Prove bad data is treated
                                              // as a denial, not a crash.
  }

  SECTION("missing permissions payload is rejected") {
    HttpError  error;  // Receives the error.
    const auto staff = PermissionManagerPrivate::
        isStaffOfService(requester,
                         std::nullopt,
                         "clinics",
                         error);  // Call with no permissions document at all.
    REQUIRE_FALSE(staff.has_value());  // Prove no record is produced.
    CHECK(error.code ==
          api::v2::Http::Status::BAD_REQUEST);  // Prove missing data is
                                                // reported as HTTP 400 Bad
                                                // Request.
  }
}

// Registers the test for the
// owner/admin id checks.
TEST_CASE("PermissionManagerPrivate checks owner and admin of a service",
          "[gatekeeper][permissions]") {
  const auto owner =
      valhalla_test::makeRequester(1, "providers");  // Client 1 is the owner.
  const auto admin =
      valhalla_test::makeRequester(2, "providers");  // Client 2 is the admin.
  const auto nobody =
      valhalla_test::makeRequester(3, "providers");  // Client 3 holds neither
                                                     // role.
  const auto permissions_j =
      valhalla_test::staffPermissions({},
                                      1,
                                      2);  // Document with owner_id=1 and
                                           // admin_id=2 but no staff roles.

  HttpError error;  // Receives an error on denial.
  CHECK(PermissionManagerPrivate::isOwnerOfService(owner,
                                                   permissions_j,
                                                   "laboratories",
                                                   error));  // Prove the owner
                                                             // is recognised.
  CHECK_FALSE(
      PermissionManagerPrivate::isOwnerOfService(nobody,
                                                 permissions_j,
                                                 "laboratories",
                                                 error));  // Prove a third
                                                           // party is not
                                                           // treated as owner.

  CHECK(PermissionManagerPrivate::isAdminOfService(admin,
                                                   permissions_j,
                                                   "laboratories",
                                                   error));  // Prove the admin
                                                             // is recognised.
  CHECK_FALSE(
      PermissionManagerPrivate::isAdminOfService(owner,
                                                 permissions_j,
                                                 "laboratories",
                                                 error));  // Prove being owner
                                                           // does not also make
                                                           // one the admin.
}

TEST_CASE(
    "PermissionManagerPrivate isOwnerOrAdminOrHasPermission resolves role "
    // Registers the test for the combined role
    // check and its documented quirks.
    "precedence",
    "[gatekeeper][permissions]") {
  const auto permissions_j =
      valhalla_test::staffPermissions({{"Doctors", {"5:16"}}},
                                      1,
                                      2);  // 16 = CAN_DELETE -- staff 5 may
                                           // delete, owner 1, admin 2.

  SECTION("owner is allowed") {
    HttpError error;  // Receives any error.
    CHECK(PermissionManagerPrivate::
              isOwnerOrAdminOrHasPermission(valhalla_test::
                                                makeRequester(1, "providers"),
                                            permissions_j,
                                            "clinics",
                                            error));  // Prove the owner passes
                                                      // regardless of staff
                                                      // powers.
  }

  SECTION("admin is allowed") {
    HttpError error;  // Receives any error.
    CHECK(PermissionManagerPrivate::
              isOwnerOrAdminOrHasPermission(valhalla_test::
                                                makeRequester(2, "providers"),
                                            permissions_j,
                                            "clinics",
                                            error));  // Prove the admin passes
                                                      // too.
  }

  SECTION("staff with CAN_DELETE is allowed") {
    HttpError error;  // Receives any error.
    CHECK(
        PermissionManagerPrivate::
            isOwnerOrAdminOrHasPermission(valhalla_test::makeRequester(5,
                                                                       "users"),
                                          permissions_j,
                                          "clinics",
                                          error));  // Prove a listed staff
                                                    // member with the right
                                                    // power passes.
  }

  SECTION("staff without CAN_DELETE is denied") {
    // Known behaviour: with a permissions object that has no owner_id /
    // admin_id keys, isOwnerOfService()/isAdminOfService() dereference
    // jsoncons::operator[] on a missing key, which throws
    // "Key not found: 'owner_id'" out of the function. This is a jsoncons
    // missing-key access, not the Store::getObject race tracked as bug 11.
    // With an owner/admin present, a CAN_READ-only member is
    // denied cleanly instead.
    const auto ownerOnly =
        valhalla_test::staffPermissions({{"Doctors", {"5:4"}}},
                                        1,
                                        2);  // Staff 5 has only read power;
                                             // owner/admin keys are present.
    HttpError error;                         // Receives the denial reason.
    CHECK_FALSE(
        PermissionManagerPrivate::
            isOwnerOrAdminOrHasPermission(valhalla_test::makeRequester(5,
                                                                       "users"),
                                          ownerOnly,
                                          "clinics",
                                          error));  // Prove a read-only member
                                                    // is denied without an
                                                    // exception when keys
                                                    // exist.
  }

  SECTION(
      "permissions object without owner/admin keys throws out of the "
      "function") {
    // jsoncons const operator[] on a missing key throws; the exception
    // escapes isOwnerOrAdminOrHasPermission and error.code is left
    // untouched (uninitialised garbage in the caller's Http::Error).
    const auto staffOnly = valhalla_test::staffPermissions(
        {{"Doctors", {"5:4"}}});  // No owner/admin keys at all, which triggers
                                  // the jsoncons missing-key throw.
    HttpError error;  // Its code is intentionally left uninitialised in
                      // this path.
    REQUIRE_THROWS_AS(PermissionManagerPrivate::isOwnerOrAdminOrHasPermission(
                          valhalla_test::makeRequester(5, "users"),
                          staffOnly,
                          "clinics",
                          error),
                      std::exception);  // Prove the known quirk: an exception
                                        // escapes rather than a clean denial.
  }

  SECTION("no permissions at all is denied") {
    HttpError error;  // Receives the denial reason.
    CHECK_FALSE(
        PermissionManagerPrivate::
            isOwnerOrAdminOrHasPermission(valhalla_test::makeRequester(5,
                                                                       "users"),
                                          std::nullopt,
                                          "clinics",
                                          error));  // Prove a missing
                                                    // permissions document is
                                                    // denied cleanly.
  }
}

// Registers the test for the simpler
// either-role check.
TEST_CASE("PermissionManagerPrivate isOwnerOrAdmin requires either role",
          "[gatekeeper][permissions]") {
  const auto permissions_j =
      valhalla_test::staffPermissions({}, 1, 2);  // Owner 1, admin 2, no staff.
  HttpError error;                                // Receives any error.

  CHECK(PermissionManagerPrivate::
            isOwnerOrAdmin(valhalla_test::makeRequester(1, "providers"),
                           permissions_j,
                           "clinics",
                           error));  // Owner qualifies.
  CHECK(PermissionManagerPrivate::
            isOwnerOrAdmin(valhalla_test::makeRequester(2, "providers"),
                           permissions_j,
                           "clinics",
                           error));  // Admin qualifies.
  CHECK_FALSE(PermissionManagerPrivate::
                  isOwnerOrAdmin(valhalla_test::makeRequester(3, "providers"),
                                 permissions_j,
                                 "clinics",
                                 error));  // A third party does not qualify.
}

// Registers the test that client self-access
// requires matching group and id.
TEST_CASE(
    "PermissionManagerPrivate assert_group_id_match enforces same group and id",
    "[gatekeeper][permissions]") {
  const auto requester =
      valhalla_test::makeRequester(11, "users");  // A client with id 11 in the
                                                  // "users" group.
  HttpError error;                                // Receives the denial reason.

  CHECK(PermissionManagerPrivate::
            assert_group_id_match(requester,
                                  "users",
                                  11,
                                  error));  // Prove matching group and id is
                                            // accepted.

  SECTION("group mismatch") {
    CHECK_FALSE(
        PermissionManagerPrivate::
            assert_group_id_match(requester,
                                  "providers",
                                  11,
                                  error));  // Prove a different group is
                                            // refused even with the right id.
    CHECK(error.code ==
          api::v2::Http::Status::FORBIDDEN);  // Prove the refusal
                                              // is reported as 403.
  }

  SECTION("id mismatch") {
    CHECK_FALSE(
        PermissionManagerPrivate::
            assert_group_id_match(requester,
                                  "users",
                                  12,
                                  error));  // Prove a different id is refused
                                            // even in the right group.
    CHECK(error.code ==
          api::v2::Http::Status::FORBIDDEN);  // Prove the refusal
                                              // is reported as 403.
  }
}

// Registers the test for who may create a
// service.
TEST_CASE(
    "PermissionManagerPrivate preServiceCreateChecks validates the creator",
    "[gatekeeper][permissions]") {
  using valhalla_test::json;  // Local alias for the JSON type.

  // Helper that builds a service
  // payload with only the supplied ids.
  const auto makeService = [](jsoncons::optional<uint64_t> owner,
                              jsoncons::optional<uint64_t> admin) {
    json service;  // Start with an empty service object.
    // Add the owner id only when supplied.
    if (owner.has_value()) {
      service["owner_id"] = owner.value();  // Write the owner id.
    }
    // Add the admin id only when supplied.
    if (admin.has_value()) {
      service["admin_id"] = admin.value();  // Write the admin id.
    }
    return std::optional<json>(
        std::move(service));  // Return the payload as an optional;
                              // std::move avoids a copy.
  };

  SECTION("provider owning and administering the service is allowed") {
    const auto requester =
        valhalla_test::makeRequester(4, "providers");  // A provider client.
    auto service_j =
        makeService(4,
                    4);  // Service owned and administered by that same client.
    HttpError error;     // Receives any error.
    CHECK(PermissionManagerPrivate::
              preServiceCreateChecks(requester,
                                     service_j,
                                     error));  // Prove a provider can create
                                               // their own service.
  }

  SECTION("non-provider is rejected") {
    const auto requester =
        valhalla_test::makeRequester(4, "users");  // A regular client, not a
                                                   // provider.
    auto service_j =
        makeService(4, 4);  // Even if ids match, the group is wrong.
    HttpError error;        // Receives the error.
    CHECK_FALSE(PermissionManagerPrivate::
                    preServiceCreateChecks(requester,
                                           service_j,
                                           error));  // Prove non-providers
                                                     // cannot create services.
    CHECK(error.code ==
          api::v2::Http::Status::BAD_REQUEST);  // Prove the reason is
                                                // reported as 400.
  }

  SECTION("owner/admin ids must match the requester") {
    const auto requester =
        valhalla_test::makeRequester(4, "providers");  // Provider 4.
    auto service_j =
        makeService(9, 4);  // Owner id 9 does not match the requester.
    HttpError error;        // Receives the error.
    CHECK_FALSE(PermissionManagerPrivate::
                    preServiceCreateChecks(requester,
                                           service_j,
                                           error));  // Prove a mismatched owner
                                                     // id is rejected.
  }

  SECTION("missing ids are rejected") {
    const auto requester =
        valhalla_test::makeRequester(4, "providers");  // Provider 4.
    auto service_j =
        makeService(std::nullopt,
                    std::nullopt);  // Neither owner nor admin id supplied.
    HttpError error;                // Receives the error.
    CHECK_FALSE(PermissionManagerPrivate::
                    preServiceCreateChecks(requester,
                                           service_j,
                                           error));  // Prove incomplete service
                                                     // data is rejected.
  }

  SECTION("no payload at all is rejected") {
    HttpError error;  // Receives the error.
    CHECK_FALSE(
        PermissionManagerPrivate::
            preServiceCreateChecks(valhalla_test::makeRequester(4, "providers"),
                                   std::nullopt,
                                   error));  // Prove a missing service document
                                             // is rejected.
  }
}

TEST_CASE(
    "PermissionManagerPrivate extract_json_value_safely returns values or a "
    // Registers the test for the safe JSON
    // extraction helper.
    "400 error",
    "[gatekeeper][permissions]") {
  using valhalla_test::json;  // Local alias for the JSON type.

  json clinicJson;              // A JSON object representing a clinic.
  clinicJson["clinic_id"] = 3;  // Give it a numeric clinic id.
  std::optional<json> service_j(
      std::move(clinicJson));  // Wrap it in optional; std::move avoids copying.
  HttpError error;             // Receives errors on failure.

  SECTION("present key") {
    const auto value = PermissionManagerPrivate::extract_json_value_safely<
        uint64_t>(service_j,
                  "clinic_id",
                  "clinic",
                  error);  // Extract an unsigned integer; the template argument
                           // says what type to expect.
    REQUIRE(value.has_value());  // Prove extraction succeeded before reading.
    CHECK(value.value() == 3);   // Prove the correct value came back.
  }

  SECTION("missing key") {
    CHECK_FALSE(
        PermissionManagerPrivate::extract_json_value_safely<uint64_t>(service_j,
                                                                      "lab_id",
                                                                      "clinic",
                                                                      error)
            .has_value());  // Prove a missing key yields no value rather
                            // than throwing.
    CHECK(error.code ==
          api::v2::Http::Status::BAD_REQUEST);  // Prove it is
                                                // reported as 400.
  }

  SECTION("empty payload") {
    CHECK_FALSE(PermissionManagerPrivate::extract_json_value_safely<
                    uint64_t>(std::nullopt, "clinic_id", "clinic", error)
                    .has_value());  // Prove a missing document also yields no
                                    // value.
  }

  SECTION("wrong type") {
    json wrongJson;  // A new JSON object.
    wrongJson["clinic_id"] =
        "three";  // Put a string where a number is expected.
    std::optional<json> wrongType(
        std::move(wrongJson));  // Wrap it for the helper.
    CHECK_FALSE(PermissionManagerPrivate::extract_json_value_safely<
                    uint64_t>(wrongType, "clinic_id", "clinic", error)
                    .has_value());  // Prove a type mismatch is rejected instead
                                    // of throwing or coercing.
  }
}

// Registers the test that ordinary
// clients may create their own records.
TEST_CASE("PermissionManager always allows creation for clients",
          "[gatekeeper][permissions]") {
  const auto requester =
      valhalla_test::makeRequester(1, "users");  // A regular client.
  HttpError error;                               // Receives any error.
  CHECK(PermissionManager{}
            .canCreate<User>(requester,
                             std::nullopt,
                             error));  // Prove creation is allowed even with no
                                       // payload; a temporary facade is
                                       // constructed inline.
}

// Registers the test that self-access
// is enforced per entity type.
TEST_CASE("PermissionManager client paths require matching group and id",
          "[gatekeeper][permissions]") {
  PermissionManager manager;  // The public facade.

  SECTION("user reading own record") {
    HttpError error;  // Receives any error.
    CHECK(manager.canRead<User>(valhalla_test::makeRequester(5, "users"),
                                5,
                                error));  // Prove a user can read their own id.
  }

  SECTION("user reading another user's record") {
    HttpError error;  // Receives the denial reason.
    CHECK_FALSE(
        manager
            .canRead<User>(valhalla_test::makeRequester(5, "users"),
                           6,
                           error));  // Prove a user cannot read a different id.
    CHECK(error.code ==
          api::v2::Http::Status::FORBIDDEN);  // Prove the denial is 403.
  }

  SECTION("provider reading a provider record") {
    HttpError error;  // Receives any error.
    CHECK(
        manager.canRead<Provider>(valhalla_test::makeRequester(9, "providers"),
                                  9,
                                  error));  // Prove the provider path also
                                            // allows self-access.
  }

  SECTION("provider denied on another provider's id") {
    HttpError error;  // Receives the denial reason.
    CHECK_FALSE(
        manager.canRead<Provider>(valhalla_test::makeRequester(9, "providers"),
                                  10,
                                  error));  // Prove providers cannot read each
                                            // other's records.
    CHECK(error.code ==
          api::v2::Http::Status::FORBIDDEN);  // Prove the denial is 403.
  }
}

// Registers the test that all client
// operations follow the same self-only rule.
TEST_CASE("PermissionManager client paths agree across the whole CRUD matrix",
          "[gatekeeper][permissions]") {
  PermissionManager manager;  // The public facade.
  const auto        requester =
      valhalla_test::makeRequester(3, "users");  // One client reused for every
                                                 // operation.

  SECTION(
      "canRead/canUpdate/canDelete/canToggleActive/canGetServices for own id") {
    HttpError error;  // Receives any errors.
    CHECK(manager.canRead<User>(requester,
                                3,
                                error));  // Reading own record is allowed.
    CHECK(manager.canUpdate<User>(requester,
                                  3,
                                  error));  // Updating own record is allowed.
    CHECK(manager.canDelete<User>(requester,
                                  3,
                                  error));  // Deleting own record is allowed.
    CHECK(manager.canToggleActive<
          User>(requester, 3, error));  // Toggling own active flag is allowed.
    CHECK(manager.canGetServices<
          User>(requester, 3, error));  // Listing own services is allowed.
  }

  SECTION("all denied for another id") {
    HttpError error;  // Receives the denial reason.
    CHECK_FALSE(manager.canRead<User>(requester,
                                      4,
                                      error));  // Reading another id is denied.
    CHECK_FALSE(
        manager.canUpdate<User>(requester,
                                4,
                                error));  // Updating another id is denied.
    CHECK_FALSE(
        manager.canDelete<User>(requester,
                                4,
                                error));  // Deleting another id is denied.
    CHECK_FALSE(manager.canToggleActive<
                User>(requester, 4, error));  // Toggling another id is denied.
    CHECK_FALSE(manager.canGetServices<User>(requester,
                                             4,
                                             error));  // Listing another id's
                                                       // services is denied.
  }
}

TEST_CASE(
    "PermissionManagerPrivate isOwnerOrAdminOrHasPermission checks the bit "
    // Registers the test that the required
    // operation's bit, not just any power, is
    // checked.
    "that matches the operation",
    "[gatekeeper][permissions]") {
  // 4 = CAN_READ, 12 = CAN_READ|CAN_WRITE, 28 = CAN_READ|CAN_WRITE|CAN_DELETE
  const auto permissions_j =
      valhalla_test::staffPermissions({{"Readers", {"5:4"}},
                                       {"Writers", {"6:12"}},
                                       {"Deleters", {"7:28"}}},
                                      1,
                                      2);  // Three staff with progressively
                                           // more power, plus owner 1 and
                                           // admin 2.

  // Small helper that asks "may client id
  // do the operation needing this bit?".
  const auto allowed = [&](const uint64_t id, const PowerLevel required) {
    HttpError error;  // Receives any error.
    return PermissionManagerPrivate::
        isOwnerOrAdminOrHasPermission(valhalla_test::makeRequester(id,
                                                                   "providers"),
                                      permissions_j,
                                      "clinics",
                                      error,
                                      required);  // Pass the required bit
                                                  // explicitly, which is the
                                                  // new overload under test.
  };

  SECTION("a read-only member can read but not write or delete") {
    CHECK(allowed(5, PowerLevel::CAN_READ));  // Client 5 has only the read bit.
    CHECK_FALSE(
        allowed(5,
                PowerLevel::CAN_WRITE));  // Prove the same member cannot write.
    CHECK_FALSE(allowed(5,
                        PowerLevel::CAN_DELETE));  // Prove the same member
                                                   // cannot delete.
  }

  SECTION("a read/write member cannot delete") {
    CHECK(allowed(6, PowerLevel::CAN_READ));   // Client 6 can read.
    CHECK(allowed(6, PowerLevel::CAN_WRITE));  // Client 6 can also write.
    CHECK_FALSE(allowed(6,
                        PowerLevel::CAN_DELETE));  // Prove the missing delete
                                                   // bit is still enforced.
  }

  SECTION("a member with all three bits can do everything") {
    CHECK(allowed(7, PowerLevel::CAN_READ));   // Client 7 can read.
    CHECK(allowed(7, PowerLevel::CAN_WRITE));  // Client 7 can write.
    CHECK(allowed(7,
                  PowerLevel::CAN_DELETE));  // Client 7 can delete, having all
                                             // three bits.
  }

  SECTION("owner and admin pass whatever bit is required") {
    CHECK(allowed(1, PowerLevel::CAN_DELETE));  // Owner passes even for delete.
    CHECK(allowed(2, PowerLevel::CAN_WRITE));   // Admin passes for write.
  }

  SECTION("someone who is not listed is denied") {
    CHECK_FALSE(allowed(99, PowerLevel::CAN_READ));
  }  // An unknown client gets no powers.
}

TEST_CASE(
    "PermissionManager canCreate<Service_t> delegates to "
    // Registers the test that the public
    // facade uses the same
    // service-creation rules.
    "preServiceCreateChecks",
    "[gatekeeper][permissions][service]") {
  using valhalla_test::json;  // Local alias for the JSON type.
  PermissionManager manager;  // The public facade.

  // Helper that builds a service payload with both
  // ids always present.
  const auto makeService = [](uint64_t owner, uint64_t admin) {
    json j;                         // Start with an empty object.
    j["owner_id"] = owner;          // Set the owner id.
    j["admin_id"] = admin;          // Set the admin id.
    return std::optional<json>(j);  // Return as an optional.
  };

  SECTION("provider owning and administering the service is allowed") {
    HttpError error;  // Receives any error.
    CHECK(manager.canCreate<
          Laboratories>(valhalla_test::makeRequester(4, "providers"),
                        makeService(4, 4),
                        error));  // Prove the facade allows a provider to
                                  // create their own service.
  }

  SECTION("non-provider is rejected") {
    HttpError error;  // Receives the error.
    CHECK_FALSE(
        manager.canCreate<Laboratories>(valhalla_test::makeRequester(4,
                                                                     "users"),
                                        makeService(4, 4),
                                        error));  // Prove a regular client
                                                  // cannot create a service.
    CHECK(error.code ==
          api::v2::Http::Status::BAD_REQUEST);  // Prove the reason is 400.
  }

  SECTION("owner id mismatch is rejected") {
    HttpError error;  // Receives the error.
    CHECK_FALSE(manager.canCreate<
                Laboratories>(valhalla_test::makeRequester(4, "providers"),
                              makeService(99, 4),
                              error));  // Prove a mismatched owner id is
                                        // rejected through the facade too.
  }

  SECTION("no data is rejected") {
    HttpError error;  // Receives the error.
    CHECK_FALSE(manager.canCreate<
                Laboratories>(valhalla_test::makeRequester(4, "providers"),
                              std::nullopt,
                              error));  // Prove a missing payload is rejected.
  }
}
