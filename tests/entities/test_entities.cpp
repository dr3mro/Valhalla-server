/**
 * @file test_entities.cpp
 * @brief Checks the SQL statements and bound parameters produced by the entity
 * and service builders.
 * @details The tests inspect generated SQL text rather than running it. They
 * cover create, read, update, delete and search statements, plus staff and
 * permission queries. The recurring theme is injection safety: user data must
 * appear as $n placeholders with a matching pqxx::params, never pasted directly
 * into the statement. Direct construction of the data types is also checked.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Every database operation is built from these helpers. If a value were ever
 * spliced into SQL text instead of being bound, an attacker could inject their
 * own SQL and read or destroy data. These tests guard that boundary and also
 * pin the exact statements so a refactor cannot silently change which table or
 * columns a query touches.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 test cases and assertions:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - SQL injection and why parameters matter:
 * https://owasp.org/www-community/attacks/SQL_Injection
 * - std::optional (a value that may be absent):
 * https://en.cppreference.com/w/cpp/utility/optional
 * - std::variant (a value that can hold one of several types):
 * https://en.cppreference.com/w/cpp/utility/variant
 * - std::unordered_set (a set of unique values):
 * https://en.cppreference.com/w/cpp/container/unordered_set
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Catch2's TEST_CASE, CHECK, REQUIRE and CHECK_FALSE macros.
#include <catch2/catch_test_macros.hpp>
#include <cstdint>            // Fixed-width integers such as uint64_t for ids.
#include <jsoncons/json.hpp>  // The JSON value type used for payloads.
#include <pqxx/pqxx>          // pqxx::params, the bound-parameter container.
#include <string>             // std::string for SQL text checks.
#include <unordered_set>      // Used for schema sets of column names.

#include "entities/base/client.hpp"   // Client-related entity helpers.
#include "entities/base/entity.hpp"   // Entity, one of the builders under test.
#include "entities/base/service.hpp"  // Service, the other builder under test.
// Types::* data wrappers used to build statements.
#include "entities/base/types.hpp"
#include "entities/people/provider.hpp"  // Provider-specific statements.
#include "entities/people/user.hpp"      // User-specific statements.
// Clinics-specific staff statements.
#include "entities/services/clinics/clinics.hpp"

using namespace api::v2;  // Bring the project's API namespace into scope for
                          // brevity.

// These tests inspect the SQL text and bound-parameter counts produced by the
// entity/service builders. The recurring theme is injection safety: user data
// must appear as $n placeholders (with a matching pqxx::params), never spliced
// into the statement.

namespace  // Anonymous namespace keeps the helper private to this file.
{
  /** @brief Builds a string-valued JSON object from key/value pairs, keeping
   * each payload in the tests short and readable. */
  jsoncons::json makeJsonObject(
      std::initializer_list<std::pair<const char*, std::string>>
          items)  // Helper that turns pairs into a JSON object.
  {
    jsoncons::json object =
        jsoncons::json::object();           // Start with an empty JSON object.
    for (const auto& [key, value] : items)  // Loop over each key/value pair.
    {
      object[key] = value;  // Store the value under its key.
    }
    return object;  // Hand the finished object back.
  }
}  // namespace

// Column names come from the payload but each value must be a placeholder, in
// jsoncons' alphabetical key order, terminated by a semicolon.
TEST_CASE("Entity create statement lists every column with placeholders",
          "[entities][sql]")  // INSERT with all fields bound.
{
  const jsoncons::json payload =
      makeJsonObject({{"name", "clinic"},
                      {"description", "general"}});  // Two fields to insert.
  const Types::Create_t create(payload,
                               5);  // Wrap them as a create request with id 5.

  Entity entity(create, "clinics");  // Build an Entity for the clinics table.
  pqxx::params params;               // Will collect the bound values.
  const auto   statement =
      entity.getSqlCreateStatement(params);  // Generate the INSERT statement.

  REQUIRE(statement.has_value());  // A statement must have been produced.
  // jsoncons uses a sorted policy, so keys are ordered alphabetically; the
  // id column is appended into the payload by the statement builder.
  CHECK(statement->find("INSERT INTO clinics (") ==
        0);  // It targets the clinics table.
  CHECK(statement->find("description,id,name") !=
        std::string::npos);  // Columns are alphabetically ordered.
  CHECK(statement->find("VALUES ($1,$2,$3)") !=
        std::string::npos);         // Three placeholders, not literal values.
  CHECK(statement->back() == ';');  // The statement ends with a semicolon.
}

TEST_CASE("Entity create statement without an id leaves the id to the database",
          "[entities][sql]")  // Client-supplied id is ignored.
{
  // A client-supplied "id" must be dropped so it can never choose its own
  // primary key.
  const jsoncons::json payload = makeJsonObject(
      {{"name", "clinic"}, {"id", "42"}});  // Include a fake id in the payload.
  const Types::Create_t create(
      payload);  // No id argument: the database will assign one.

  Entity       entity(create, "clinics");  // Build the Entity.
  pqxx::params params;                     // Will collect bound values.
  const auto   statement =
      entity.getSqlCreateStatement(params);  // Generate the statement.

  REQUIRE(statement.has_value());  // A statement must exist.
  CHECK(statement->find("INSERT INTO clinics (name)") ==
        0);  // Only "name" is a column; "id" was dropped.
  CHECK(statement->find("VALUES ($1)") !=
        std::string::npos);  // One placeholder for the name.
  CHECK(statement->find("RETURNING *;") !=
        std::string::npos);  // The new row is returned to the caller.
  CHECK(statement->find("id") ==
        std::string::npos);  // No literal "id" text leaked into the SQL.
}

// Reads go through the *_safe view and bind the id rather than embedding it,
// so a caller-supplied id can never alter the query structure.
TEST_CASE("Entity read statement targets the _safe view and binds the id",
          "[entities][sql]")  // SELECT from the safe view.
{
  const std::unordered_set<std::string> schema =
      {"name", "dob"};                  // The columns to select.
  const Types::Read_t read(schema, 9);  // Read request for id 9.

  Entity       entity(read, "users");  // Build the Entity.
  pqxx::params params;                 // Will collect bound values.
  const auto   statement =
      entity.getSqlReadStatement(params);  // Generate the SELECT.

  REQUIRE(statement.has_value());          // A statement must exist.
  CHECK(statement->find("SELECT ") == 0);  // It starts as a SELECT.
  CHECK(statement->find(" FROM users_safe WHERE id=$1 LIMIT 1;") !=
        std::string::npos);  // Targets the safe view with a bound id.
  CHECK(statement->find("name") !=
        std::string::npos);  // Requested column "name" is present.
  CHECK(statement->find("dob") !=
        std::string::npos);  // Requested column "dob" is present.
  CHECK(statement->find('9') ==
        std::string::npos);   // The id value 9 is not in the text.
  CHECK(params.size() == 1);  // Exactly one value was bound.
}

TEST_CASE("Entity read statement with an empty schema selects all columns",
          "[entities][sql]")  // Empty schema means SELECT *.
{
  const Types::Read_t read({}, 3);  // No columns specified.

  Entity       entity(read, "users");  // Build the Entity.
  pqxx::params params;                 // Will collect bound values.
  const auto   statement =
      entity.getSqlReadStatement(params);  // Generate the SELECT.

  REQUIRE(statement.has_value());  // A statement must exist.
  CHECK(statement->find("SELECT * FROM users_safe WHERE id=$1 LIMIT 1;") ==
        0);  // Exactly the wildcard form is produced.
}

TEST_CASE("Entity update statement builds a SET clause with parameters",
          "[entities][sql]")  // UPDATE with bound values.
{
  const jsoncons::json payload =
      makeJsonObject({{"name", "renamed"}});  // One field to change.
  const Types::Update_t update(payload, 12);  // Update request for id 12.

  Entity       entity(update, "users");  // Build the Entity.
  pqxx::params params;                   // Will collect bound values.
  const auto   statement =
      entity.getSqlUpdateStatement(params);  // Generate the UPDATE.

  REQUIRE(statement.has_value());  // A statement must exist.
  CHECK(statement->find("UPDATE users SET") ==
        0);  // It targets the users table.
  CHECK(statement->find("name = $1") !=
        std::string::npos);  // The new value is a placeholder.
  CHECK(statement->find("WHERE id=$2 RETURNING *;") !=
        std::string::npos);  // The id is a second placeholder.
  CHECK(statement->find("renamed") ==
        std::string::npos);   // The literal value is not in the text.
  CHECK(params.size() == 2);  // Two values were bound.
}

TEST_CASE("Entity delete statement removes by a bound id",
          "[entities][sql]")  // DELETE with a bound id.
{
  const Types::Delete_t del(21);  // Delete request for id 21.

  Entity       entity(del, "clinics");  // Build the Entity.
  pqxx::params params;                  // Will collect bound values.
  const auto   statement =
      entity.getSqlDeleteStatement(params);  // Generate the DELETE.

  REQUIRE(statement.has_value());  // A statement must exist.
  CHECK(*statement ==
        "DELETE FROM clinics WHERE id=$1 RETURNING id;");  // The exact expected
                                                           // statement.
  CHECK(params.size() == 1);                               // One value bound.
}

TEST_CASE(
    "Entity search statement parameterises the keyword and honours paging",
    "[entities][sql]")  // Search with paging.
{
  // A complete search request: keyword, filter, ordering and paging.
  const jsoncons::json search_j = jsoncons::json::parse(R"({
        "keyword": "fever",
        "filter": "name",
        "order_by": "created_at",
        "direction": 0,
        "limit": 10,
        "offset": 20
    })");
  bool success = false;  // Set true by the parser if the JSON is valid.
  const Types::Search_t search(search_j, success);  // Parse it into a Search_t.
  REQUIRE(success);  // Parsing must have succeeded.

  Entity       entity(search, "patients");  // Build the Entity.
  pqxx::params params;                      // Will collect bound values.
  const auto   statement =
      entity.getSqlSearchStatement(params);  // Generate the search SELECT.

  REQUIRE(statement.has_value());  // A statement must exist.
  CHECK(statement->find(
            "SELECT * FROM patients_safe WHERE name::text ILIKE $1") !=
        std::string::npos);  // Keyword is bound; filter/order are identifiers.
  CHECK(statement->find("ORDER BY created_at ASC") !=
        std::string::npos);  // Direction 0 maps to ascending.
  // LIMIT is limit + 1 (the extra row detects a next page); keyword, limit
  // and offset are all bound.
  CHECK(statement->find("LIMIT $2 OFFSET $3;") !=
        std::string::npos);   // Paging values are bound too.
  CHECK(params.size() == 3);  // Three values bound in total.
}

TEST_CASE("Entity exposes its group name and data variant",
          "[entities][sql]")  // Accessors on Entity.
{
  const Types::Delete_t del(1);                // Some data to construct with.
  const Entity          entity(del, "users");  // Build the Entity.

  CHECK(entity.getGroupName() == "users");  // The table/group name is stored.
  CHECK_NOTHROW(std::get<Types::Delete_t>(
      entity.getData()));  // The data variant holds a Delete_t without
                           // throwing.
}

TEST_CASE("Client suspend and activate statements bind the state and the id",
          "[entities][sql][params]")  // User state changes.
{
  const Types::SuspendData suspendData(55);    // Suspend request for client 55.
  User                     user(suspendData);  // Build the User helper.
  pqxx::params             params;             // Will collect bound values.

  const auto suspend =
      user.getSqlSuspendStatement(params);  // Generate the suspend UPDATE.
  REQUIRE(suspend.has_value());             // A statement must exist.
  CHECK(*suspend ==
        // Exact expected SQL.
        "UPDATE users SET active=$1 WHERE id=$2 RETURNING id,active;");
  CHECK(params.size() == 2);  // Two bound values.
  CHECK(suspend->find("55") ==
        std::string::npos);  // The id is not embedded in text.

  pqxx::params activate_params;  // Fresh params for the second statement.
  const auto   activate = user.getSqlActivateStatement(
      activate_params);           // Generate the activate UPDATE.
  REQUIRE(activate.has_value());  // A statement must exist.
  CHECK(*activate ==
        // Same shape as suspend.
        "UPDATE users SET active=$1 WHERE id=$2 RETURNING id,active;");
  CHECK(activate_params.size() == 2);  // Two bound values.
}

TEST_CASE("Get-services statements bind the client id",
          "[entities][sql][params]")  // Service lookup for users and providers.
{
  const Types::Data_t data(33);     // Request for client 33.
  pqxx::params        user_params;  // Bound values for the user query.

  User       user(data);  // Build the User helper.
  const auto user_stmt =
      user.getSqlGetServicesStatement(user_params);  // Generate its statement.
  REQUIRE(user_stmt.has_value());                    // A statement must exist.
  CHECK(*user_stmt ==
        "SELECT id FROM users WHERE id = $1");  // Exact expected SQL.
  CHECK(user_params.size() == 1);               // One bound value.

  pqxx::params provider_params;  // Bound values for the provider query.
  Provider     provider(data);   // Build the Provider helper.
  const auto   provider_stmt = provider.getSqlGetServicesStatement(
      provider_params);                // Generate its statement.
  REQUIRE(provider_stmt.has_value());  // A statement must exist.
  CHECK(provider_params.size() == 2);  // It binds two values.
  CHECK(provider_stmt->find("33") ==
        std::string::npos);  // The id is not embedded.
  CHECK(provider_stmt->find("$1") !=
        std::string::npos);  // First placeholder present.
  CHECK(provider_stmt->find("$2") !=
        std::string::npos);  // Second placeholder present.
}

TEST_CASE("Staff statements bind the nominee id and the entity id",
          "[entities][sql][params]")  // Staff add/remove SQL.
{
  jsoncons::json staff_j;                   // Build a complete staff payload.
  staff_j["nominee_id"]    = uint64_t{10};  // Who is being added.
  staff_j["entity_id"]     = uint64_t{20};  // Which entity they join.
  staff_j["role"]          = "Doctor";      // Their role.
  staff_j["email"]         = "doc@example.com";  // Their email.
  staff_j["nominee_group"] = "providers";        // Their group.
  staff_j["entity_name"]   = "City Clinic";      // The entity's display name.
  const Types::StaffData sd(staff_j);            // Wrap it as StaffData.
  Clinics                clinic(sd);             // Build the Clinics helper.

  pqxx::params params;  // Bound values for the add statement.
  const auto   add =
      clinic.getSqlAddStaffStatement(params);  // Generate the add UPDATE.
  REQUIRE(add.has_value());                    // A statement must exist.
  CHECK(params.size() == 2);                   // Two bound values.
  CHECK(add->find("WHERE id=$") !=
        std::string::npos);  // The entity id is a placeholder.
  CHECK(add->find("to_jsonb($") !=
        std::string::npos);  // The nominee data is bound as JSON.
  CHECK(add->find("10") ==
        std::string::npos);  // Nominee id 10 is not in the text.
  CHECK(add->find("20") ==
        std::string::npos);  // Entity id 20 is not in the text.

  pqxx::params remove_params;  // Bound values for the remove statement.
  const auto   remove = clinic.getSqlRemoveStaffStatement(
      remove_params);                // Generate the remove UPDATE.
  REQUIRE(remove.has_value());       // A statement must exist.
  CHECK(remove_params.size() == 2);  // Two bound values.
  CHECK(remove->find("elem <> to_jsonb($") !=
        std::string::npos);  // It filters the array with a bound value.
}

TEST_CASE("SuspendData stores the client id",
          "[entities][types]")  // Data wrapper accessors.
{
  const Types::SuspendData sd(42);  // Construct with id 42.
  CHECK(sd.client_id == 42);        // The id is stored.
}

TEST_CASE("StaffData parses a complete JSON object",
          "[entities][types]")  // All required fields present.
{
  jsoncons::json staff_j;                        // Build a full payload.
  staff_j["nominee_id"]    = uint64_t{10};       // Nominee id.
  staff_j["entity_id"]     = uint64_t{20};       // Entity id.
  staff_j["role"]          = "Doctor";           // Role.
  staff_j["email"]         = "doc@example.com";  // Email.
  staff_j["nominee_group"] = "providers";        // Group.
  staff_j["entity_name"]   = "City Clinic";      // Entity name.

  const Types::StaffData sd(staff_j);      // Parse it.
  CHECK(sd.parse_status == true);          // All fields were found.
  CHECK(sd.nominee_id == 10);              // Nominee id parsed.
  CHECK(sd.entity_id == 20);               // Entity id parsed.
  CHECK(sd.role == "Doctor");              // Role parsed.
  CHECK(sd.email == "doc@example.com");    // Email parsed.
  CHECK(sd.nominee_group == "providers");  // Group parsed.
  CHECK(sd.entity_name == "City Clinic");  // Entity name parsed.
  CHECK_FALSE(
      sd.project_name.empty());  // A project name is derived from the data.
}

TEST_CASE("StaffData sets parse_status=false on a missing key",
          "[entities][types]")  // Missing required field.
{
  jsoncons::json staff_j;                // Build an incomplete payload.
  staff_j["nominee_id"] = uint64_t{10};  // Only one field is present.
  // entity_id and other required keys are absent

  const Types::StaffData sd(staff_j);  // Parse it.
  CHECK(sd.parse_status == false);     // Parsing is flagged as failed.
}

TEST_CASE("StaffData::toInviteJson adds the required invite fields",
          "[entities][types]")  // Invite payload builder.
{
  jsoncons::json staff_j;                          // Build a full payload.
  staff_j["nominee_id"]    = uint64_t{1};          // Nominee id.
  staff_j["entity_id"]     = uint64_t{2};          // Entity id.
  staff_j["role"]          = "Nurse";              // Role.
  staff_j["email"]         = "nurse@example.com";  // Email.
  staff_j["nominee_group"] = "providers";          // Group.
  staff_j["entity_name"]   = "Central Hospital";   // Entity name.

  Types::StaffData sd(
      staff_j);              // Parse it (non-const: toInviteJson may modify).
  REQUIRE(sd.parse_status);  // Parsing succeeded.

  jsoncons::json invite =
      jsoncons::json::object();      // Start with an empty invite object.
  invite["some_field"] = "initial";  // Add an unrelated field to prove others
                                     // are added alongside.

  const bool ok = sd.toInviteJson(invite);  // Add the required invite fields.
  CHECK(ok);                                // The operation reports success.
  CHECK(invite.contains("subject"));        // Invite subject added.
  CHECK(invite.contains("template"));       // Template added.
  CHECK(invite.contains("link"));           // Link added.
  CHECK(invite.contains("project_name"));   // Project name added.
  CHECK(invite.contains("generate_body"));  // Generate-body flag added.
}

TEST_CASE("Entity read statement keeps the requested columns",
          "[entities][sql]")  // Selective column read.
{
  const std::unordered_set<std::string> schema = {
      "name"};                           // Only one column requested.
  const Types::Read_t read(schema, 15);  // Read request for id 15.

  Entity       entity(read, "clinics");  // Build the Entity.
  pqxx::params params;                   // Bound values.
  const auto   statement =
      entity.getSqlReadStatement(params);  // Generate the SELECT.

  REQUIRE(statement.has_value());  // A statement must exist.
  CHECK(statement->find("FROM clinics_safe WHERE id=$1 LIMIT 1;") !=
        std::string::npos);  // Targets the safe view with a bound id.
  CHECK(statement->find("name") !=
        std::string::npos);  // The requested column is present.
}

TEST_CASE("Entity delete statement is rebuilt cleanly on every call",
          "[entities][sql]")  // No state leaks between calls.
{
  const Types::Delete_t del(99);  // Delete request for id 99.
  Entity                entity(del, "appointments");  // Build the Entity.
  pqxx::params          first;  // Params for a first call.
  REQUIRE(entity.getSqlDeleteStatement(first)
              .has_value());  // First generation succeeds.
  pqxx::params params;        // Fresh params for a second call.
  const auto   statement =
      entity.getSqlDeleteStatement(params);  // Generate again.

  REQUIRE(statement.has_value());  // The second statement exists.
  CHECK(*statement ==
        // It is identical to a fresh one.
        "DELETE FROM appointments WHERE id=$1 RETURNING id;");
  CHECK(params.size() ==
        1);  // One bound value, not accumulated from the first call.
}

// ── Service SQL builders ────────────────────────────────────────────────────

namespace  // Anonymous namespace keeps the helper private to this file.
{
  /** @brief Produces a fully populated StaffData for the staff SQL-builder
   * tests.
   *
   * All required keys are present so parse_status is true and the builders
   * under test are the only thing being varied. */
  Types::StaffData makeStaffData(
      uint64_t           nominee_id = 42,
      uint64_t           entity_id  = 7,
      const std::string& nominee_group =
          "providers")  // Helper with sensible defaults.
  {
    jsoncons::json j;                        // Build the payload.
    j["nominee_id"]    = nominee_id;         // Who is being added.
    j["entity_id"]     = entity_id;          // Which entity they join.
    j["role"]          = "Doctor";           // Role.
    j["email"]         = "doc@example.com";  // Email.
    j["nominee_group"] = nominee_group;      // Group (varies in one test).
    j["entity_name"]   = "City Clinic";      // Entity name.
    return Types::StaffData(j);              // Parse and return it.
  }
}  // namespace

TEST_CASE(
    "Service getSqlAddStaffStatement builds an UPDATE with the correct table",
    "[entities][sql][service]")  // Service-level add.
{
  const auto sd = makeStaffData(42, 7, "providers");  // A valid staff payload.
  Service    svc(sd, "clinics");  // Build a Service for clinics.
  pqxx::params params;            // Bound values.
  const auto   stmt =
      svc.getSqlAddStaffStatement(params);  // Generate the add statement.

  REQUIRE(stmt.has_value());                 // A statement must exist.
  CHECK(stmt->find("UPDATE clinics") == 0);  // It targets the clinics table.
  CHECK(stmt->find("42") ==
        std::string::npos);  // nominee_id is bound, not embedded
  CHECK(stmt->find("RETURNING id;") !=
        std::string::npos);   // The changed row id is returned.
  CHECK(params.size() == 2);  // Two bound values.
}

TEST_CASE(
    "Service getSqlRemoveStaffStatement builds a filtered jsonb_agg UPDATE",
    "[entities][sql][service]")  // Service-level remove.
{
  const auto sd = makeStaffData(99, 5, "providers");  // A valid staff payload.
  Service    svc(sd, "laboratories");  // Build a Service for laboratories.
  pqxx::params params;                 // Bound values.
  const auto   stmt =
      svc.getSqlRemoveStaffStatement(params);  // Generate the remove statement.

  REQUIRE(stmt.has_value());                      // A statement must exist.
  CHECK(stmt->find("UPDATE laboratories") == 0);  // It targets the given table.
  CHECK(stmt->find("99") ==
        std::string::npos);  // nominee_id being removed is bound
  CHECK(stmt->find("jsonb_agg") !=
        std::string::npos);  // It rebuilds the array with jsonb_agg.
  CHECK(stmt->find("RETURNING id;") !=
        std::string::npos);   // The changed row id is returned.
  CHECK(params.size() == 2);  // Two bound values.
}

TEST_CASE(
    "Service::getServicePermissionsQuery returns a SELECT for the given "
    "service and id",
    "[entities][sql][service]")  // Permissions query.
{
  const auto stmt =
      Service::getServicePermissionsQuery("clinics",
                                          13);  // Ask for clinics permissions
                                                // for id 13.

  REQUIRE(stmt.has_value());  // A statement must exist.
  CHECK(stmt->find("SELECT owner_id,admin_id,staff FROM clinics") ==
        0);  // It reads the permission columns.
  CHECK(stmt->find("13") != std::string::npos);  // Here the id is embedded (a
                                                 // trusted internal value).
  CHECK(stmt->find("LIMIT 1;") !=
        std::string::npos);  // Only one row is returned.
}

TEST_CASE(
    "Service getSqlAddStaffStatement returns nullopt when data variant is "
    "wrong type",
    "[entities][sql][service]")  // Wrong variant is rejected.
{
  // Constructing a Service with Create_t (not StaffData) makes getData() hold
  // the wrong variant — std::get<StaffData> throws and the method returns
  // nullopt.
  const jsoncons::json payload =
      makeJsonObject({{"name", "test"}});  // A create-style payload.
  const Types::Create_t create(payload,
                               1);  // Wrapped as Create_t, not StaffData.
  Service svc(create, "clinics");   // Build a Service with the wrong data type.
  pqxx::params params;              // Bound values.
  CHECK_FALSE(svc.getSqlAddStaffStatement(params)
                  .has_value());  // No statement is produced.
}

TEST_CASE(
    "Service getSqlRemoveStaffStatement returns nullopt when data variant is "
    "wrong type",
    "[entities][sql][service]")  // Wrong variant is rejected again.
{
  const Types::Delete_t del(1);                  // A Delete_t, not StaffData.
  Service               svc(del, "pharmacies");  // Build a Service with it.
  pqxx::params          params;                  // Bound values.
  CHECK_FALSE(svc.getSqlRemoveStaffStatement(params)
                  .has_value());  // No statement is produced.
}

// ── Types direct construction ───────────────────────────────────────────────

TEST_CASE("Types::Create_t stores the payload and id",
          "[entities][types]")  // Create_t accessors.
{
  const jsoncons::json payload =
      makeJsonObject({{"field", "value"}});   // A simple payload.
  const Types::Create_t create(payload, 77);  // Wrap with id 77.

  REQUIRE(create.get_id().has_value());        // An id is stored.
  CHECK(create.get_id().value() == 77);        // It is 77.
  CHECK(create.get_data().contains("field"));  // The payload field is kept.
}

TEST_CASE("Types::Read_t stores the schema set and id",
          "[entities][types]")  // Read_t accessors.
{
  const std::unordered_set<std::string> schema = {"name",
                                                  "dob",
                                                  "email"};  // Columns to read.
  const Types::Read_t                   read(schema, 55);    // Wrap with id 55.

  REQUIRE(read.get_id().has_value());          // An id is stored.
  CHECK(read.get_id().value() == 55);          // It is 55.
  CHECK(read.get_data().count("name") == 1);   // "name" is in the schema.
  CHECK(read.get_data().count("email") == 1);  // "email" is in the schema.
}

TEST_CASE("Types::Update_t stores the payload and id",
          "[entities][types]")  // Update_t accessors.
{
  const jsoncons::json payload =
      makeJsonObject({{"status", "active"}});  // A simple payload.
  const Types::Update_t update(payload, 11);   // Wrap with id 11.

  REQUIRE(update.get_id().has_value());  // An id is stored.
  CHECK(update.get_id().value() == 11);  // It is 11.
  CHECK(update.get_data()["status"].as_string() ==
        "active");  // The payload value is kept.
}

TEST_CASE("Types::Delete_t stores the id",
          "[entities][types]")  // Delete_t accessors.
{
  const Types::Delete_t del(999);      // Wrap id 999.
  REQUIRE(del.get_id().has_value());   // An id is stored.
  CHECK(del.get_id().value() == 999);  // It is 999.
}

TEST_CASE("Types::Data_t stores the id",
          "[entities][types]")  // Data_t accessors.
{
  const Types::Data_t data(123);        // Wrap id 123.
  REQUIRE(data.get_id().has_value());   // An id is stored.
  CHECK(data.get_id().value() == 123);  // It is 123.
}

// PermissionManager Service_t canCreate is tested in test_permissions.cpp
