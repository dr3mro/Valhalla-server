#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <jsoncons/json.hpp>
#include <pqxx/pqxx>
#include <string>
#include <unordered_set>

#include "entities/base/client.hpp"
#include "entities/base/entity.hpp"
#include "entities/base/service.hpp"
#include "entities/base/types.hpp"
#include "entities/people/provider.hpp"
#include "entities/people/user.hpp"
#include "entities/services/clinics/clinics.hpp"

using namespace api::v2;

namespace
{
    jsoncons::json makeJsonObject(std::initializer_list<std::pair<const char*, std::string>> items)
    {
        jsoncons::json object = jsoncons::json::object();
        for (const auto& [key, value] : items)
        {
            object[key] = value;
        }
        return object;
    }
}  // namespace

TEST_CASE("Entity create statement lists every column with placeholders", "[entities][sql]")
{
    const jsoncons::json  payload = makeJsonObject({{"name", "clinic"}, {"description", "general"}});
    const Types::Create_t create(payload, 5);

    Entity       entity(create, "clinics");
    pqxx::params params;
    const auto   statement = entity.getSqlCreateStatement(params);

    REQUIRE(statement.has_value());
    // jsoncons uses a sorted policy, so keys are ordered alphabetically; the
    // id column is appended into the payload by the statement builder.
    CHECK(statement->find("INSERT INTO clinics (") == 0);
    CHECK(statement->find("description,id,name") != std::string::npos);
    CHECK(statement->find("VALUES ($1,$2,$3)") != std::string::npos);
    CHECK(statement->back() == ';');
}

TEST_CASE("Entity create statement without an id leaves the id to the database", "[entities][sql]")
{
    // A client-supplied "id" must be dropped so it can never choose its own primary key.
    const jsoncons::json  payload = makeJsonObject({{"name", "clinic"}, {"id", "42"}});
    const Types::Create_t create(payload);

    Entity       entity(create, "clinics");
    pqxx::params params;
    const auto   statement = entity.getSqlCreateStatement(params);

    REQUIRE(statement.has_value());
    CHECK(statement->find("INSERT INTO clinics (name)") == 0);
    CHECK(statement->find("VALUES ($1)") != std::string::npos);
    CHECK(statement->find("RETURNING *;") != std::string::npos);
    CHECK(statement->find("id") == std::string::npos);
}

TEST_CASE("Entity read statement targets the _safe view and binds the id", "[entities][sql]")
{
    const std::unordered_set<std::string> schema = {"name", "dob"};
    const Types::Read_t                   read(schema, 9);

    Entity       entity(read, "users");
    pqxx::params params;
    const auto   statement = entity.getSqlReadStatement(params);

    REQUIRE(statement.has_value());
    CHECK(statement->find("SELECT ") == 0);
    CHECK(statement->find(" FROM users_safe WHERE id=$1 LIMIT 1;") != std::string::npos);
    CHECK(statement->find("name") != std::string::npos);
    CHECK(statement->find("dob") != std::string::npos);
    CHECK(statement->find('9') == std::string::npos);
    CHECK(params.size() == 1);
}

TEST_CASE("Entity read statement with an empty schema selects all columns", "[entities][sql]")
{
    const Types::Read_t read({}, 3);

    Entity       entity(read, "users");
    pqxx::params params;
    const auto   statement = entity.getSqlReadStatement(params);

    REQUIRE(statement.has_value());
    CHECK(statement->find("SELECT * FROM users_safe WHERE id=$1 LIMIT 1;") == 0);
}

TEST_CASE("Entity update statement builds a SET clause with parameters", "[entities][sql]")
{
    const jsoncons::json  payload = makeJsonObject({{"name", "renamed"}});
    const Types::Update_t update(payload, 12);

    Entity       entity(update, "users");
    pqxx::params params;
    const auto   statement = entity.getSqlUpdateStatement(params);

    REQUIRE(statement.has_value());
    CHECK(statement->find("UPDATE users SET") == 0);
    CHECK(statement->find("name = $1") != std::string::npos);
    CHECK(statement->find("WHERE id=$2 RETURNING *;") != std::string::npos);
    CHECK(statement->find("renamed") == std::string::npos);
    CHECK(params.size() == 2);
}

TEST_CASE("Entity delete statement removes by a bound id", "[entities][sql]")
{
    const Types::Delete_t del(21);

    Entity       entity(del, "clinics");
    pqxx::params params;
    const auto   statement = entity.getSqlDeleteStatement(params);

    REQUIRE(statement.has_value());
    CHECK(*statement == "DELETE FROM clinics WHERE id=$1 RETURNING id;");
    CHECK(params.size() == 1);
}

TEST_CASE("Entity search statement parameterises the keyword and honours paging", "[entities][sql]")
{
    const jsoncons::json  search_j = jsoncons::json::parse(R"({
        "keyword": "fever",
        "filter": "name",
        "order_by": "created_at",
        "direction": 0,
        "limit": 10,
        "offset": 20
    })");
    bool                  success  = false;
    const Types::Search_t search(search_j, success);
    REQUIRE(success);

    Entity       entity(search, "patients");
    pqxx::params params;
    const auto   statement = entity.getSqlSearchStatement(params);

    REQUIRE(statement.has_value());
    CHECK(statement->find("SELECT * FROM patients_safe WHERE name::text ILIKE $1") != std::string::npos);
    CHECK(statement->find("ORDER BY created_at ASC") != std::string::npos);
    // LIMIT is limit + 1 (the extra row detects a next page); keyword, limit and offset are all bound.
    CHECK(statement->find("LIMIT $2 OFFSET $3;") != std::string::npos);
    CHECK(params.size() == 3);
}

TEST_CASE("Entity exposes its group name and data variant", "[entities][sql]")
{
    const Types::Delete_t del(1);
    const Entity          entity(del, "users");

    CHECK(entity.getGroupName() == "users");
    CHECK_NOTHROW(std::get<Types::Delete_t>(entity.getData()));
}

TEST_CASE("Client suspend and activate statements bind the state and the id", "[entities][sql][params]")
{
    const Types::SuspendData suspendData(55);
    User                     user(suspendData);
    pqxx::params             params;

    const auto suspend = user.getSqlSuspendStatement(params);
    REQUIRE(suspend.has_value());
    CHECK(*suspend == "UPDATE users SET active=$1 WHERE id=$2 RETURNING id,active;");
    CHECK(params.size() == 2);
    CHECK(suspend->find("55") == std::string::npos);

    pqxx::params activate_params;
    const auto   activate = user.getSqlActivateStatement(activate_params);
    REQUIRE(activate.has_value());
    CHECK(*activate == "UPDATE users SET active=$1 WHERE id=$2 RETURNING id,active;");
    CHECK(activate_params.size() == 2);
}

TEST_CASE("Get-services statements bind the client id", "[entities][sql][params]")
{
    const Types::Data_t data(33);
    pqxx::params        user_params;

    User       user(data);
    const auto user_stmt = user.getSqlGetServicesStatement(user_params);
    REQUIRE(user_stmt.has_value());
    CHECK(*user_stmt == "SELECT id FROM users WHERE id = $1");
    CHECK(user_params.size() == 1);

    pqxx::params provider_params;
    Provider     provider(data);
    const auto   provider_stmt = provider.getSqlGetServicesStatement(provider_params);
    REQUIRE(provider_stmt.has_value());
    CHECK(provider_params.size() == 2);
    CHECK(provider_stmt->find("33") == std::string::npos);
    CHECK(provider_stmt->find("$1") != std::string::npos);
    CHECK(provider_stmt->find("$2") != std::string::npos);
}

TEST_CASE("Staff statements bind the nominee id and the entity id", "[entities][sql][params]")
{
    jsoncons::json staff_j;
    staff_j["nominee_id"]    = uint64_t{10};
    staff_j["entity_id"]     = uint64_t{20};
    staff_j["role"]          = "Doctor";
    staff_j["email"]         = "doc@example.com";
    staff_j["nominee_group"] = "providers";
    staff_j["entity_name"]   = "City Clinic";
    const Types::StaffData sd(staff_j);
    Clinics                clinic(sd);

    pqxx::params params;
    const auto   add = clinic.getSqlAddStaffStatement(params);
    REQUIRE(add.has_value());
    CHECK(params.size() == 2);
    CHECK(add->find("WHERE id=$") != std::string::npos);
    CHECK(add->find("to_jsonb($") != std::string::npos);
    CHECK(add->find("10") == std::string::npos);
    CHECK(add->find("20") == std::string::npos);

    pqxx::params remove_params;
    const auto   remove = clinic.getSqlRemoveStaffStatement(remove_params);
    REQUIRE(remove.has_value());
    CHECK(remove_params.size() == 2);
    CHECK(remove->find("elem <> to_jsonb($") != std::string::npos);
}

TEST_CASE("SuspendData stores the client id", "[entities][types]")
{
    const Types::SuspendData sd(42);
    CHECK(sd.client_id == 42);
}

TEST_CASE("StaffData parses a complete JSON object", "[entities][types]")
{
    jsoncons::json staff_j;
    staff_j["nominee_id"]    = uint64_t{10};
    staff_j["entity_id"]     = uint64_t{20};
    staff_j["role"]          = "Doctor";
    staff_j["email"]         = "doc@example.com";
    staff_j["nominee_group"] = "providers";
    staff_j["entity_name"]   = "City Clinic";

    const Types::StaffData sd(staff_j);
    CHECK(sd.parse_status == true);
    CHECK(sd.nominee_id == 10);
    CHECK(sd.entity_id == 20);
    CHECK(sd.role == "Doctor");
    CHECK(sd.email == "doc@example.com");
    CHECK(sd.nominee_group == "providers");
    CHECK(sd.entity_name == "City Clinic");
    CHECK_FALSE(sd.project_name.empty());
}

TEST_CASE("StaffData sets parse_status=false on a missing key", "[entities][types]")
{
    jsoncons::json staff_j;
    staff_j["nominee_id"] = uint64_t{10};
    // entity_id and other required keys are absent

    const Types::StaffData sd(staff_j);
    CHECK(sd.parse_status == false);
}

TEST_CASE("StaffData::toInviteJson adds the required invite fields", "[entities][types]")
{
    jsoncons::json staff_j;
    staff_j["nominee_id"]    = uint64_t{1};
    staff_j["entity_id"]     = uint64_t{2};
    staff_j["role"]          = "Nurse";
    staff_j["email"]         = "nurse@example.com";
    staff_j["nominee_group"] = "providers";
    staff_j["entity_name"]   = "Central Hospital";

    Types::StaffData sd(staff_j);
    REQUIRE(sd.parse_status);

    jsoncons::json invite = jsoncons::json::object();
    invite["some_field"]  = "initial";

    const bool ok = sd.toInviteJson(invite);
    CHECK(ok);
    CHECK(invite.contains("subject"));
    CHECK(invite.contains("template"));
    CHECK(invite.contains("link"));
    CHECK(invite.contains("project_name"));
    CHECK(invite.contains("generate_body"));
}

TEST_CASE("Entity read statement keeps the requested columns", "[entities][sql]")
{
    const std::unordered_set<std::string> schema = {"name"};
    const Types::Read_t                   read(schema, 15);

    Entity       entity(read, "clinics");
    pqxx::params params;
    const auto   statement = entity.getSqlReadStatement(params);

    REQUIRE(statement.has_value());
    CHECK(statement->find("FROM clinics_safe WHERE id=$1 LIMIT 1;") != std::string::npos);
    CHECK(statement->find("name") != std::string::npos);
}

TEST_CASE("Entity delete statement is rebuilt cleanly on every call", "[entities][sql]")
{
    const Types::Delete_t del(99);
    Entity                entity(del, "appointments");
    pqxx::params          first;
    REQUIRE(entity.getSqlDeleteStatement(first).has_value());
    pqxx::params params;
    const auto   statement = entity.getSqlDeleteStatement(params);

    REQUIRE(statement.has_value());
    CHECK(*statement == "DELETE FROM appointments WHERE id=$1 RETURNING id;");
    CHECK(params.size() == 1);
}

// ── Service SQL builders ────────────────────────────────────────────────────

namespace
{
    Types::StaffData makeStaffData(uint64_t nominee_id = 42, uint64_t entity_id = 7, const std::string& nominee_group = "providers")
    {
        jsoncons::json j;
        j["nominee_id"]    = nominee_id;
        j["entity_id"]     = entity_id;
        j["role"]          = "Doctor";
        j["email"]         = "doc@example.com";
        j["nominee_group"] = nominee_group;
        j["entity_name"]   = "City Clinic";
        return Types::StaffData(j);
    }
}  // namespace

TEST_CASE("Service getSqlAddStaffStatement builds an UPDATE with the correct table", "[entities][sql][service]")
{
    const auto   sd = makeStaffData(42, 7, "providers");
    Service      svc(sd, "clinics");
    pqxx::params params;
    const auto   stmt = svc.getSqlAddStaffStatement(params);

    REQUIRE(stmt.has_value());
    CHECK(stmt->find("UPDATE clinics") == 0);
    CHECK(stmt->find("42") == std::string::npos);  // nominee_id is bound, not embedded
    CHECK(stmt->find("RETURNING id;") != std::string::npos);
    CHECK(params.size() == 2);
}

TEST_CASE("Service getSqlRemoveStaffStatement builds a filtered jsonb_agg UPDATE", "[entities][sql][service]")
{
    const auto   sd = makeStaffData(99, 5, "providers");
    Service      svc(sd, "laboratories");
    pqxx::params params;
    const auto   stmt = svc.getSqlRemoveStaffStatement(params);

    REQUIRE(stmt.has_value());
    CHECK(stmt->find("UPDATE laboratories") == 0);
    CHECK(stmt->find("99") == std::string::npos);  // nominee_id being removed is bound
    CHECK(stmt->find("jsonb_agg") != std::string::npos);
    CHECK(stmt->find("RETURNING id;") != std::string::npos);
    CHECK(params.size() == 2);
}

TEST_CASE("Service::getServicePermissionsQuery returns a SELECT for the given service and id", "[entities][sql][service]")
{
    const auto stmt = Service::getServicePermissionsQuery("clinics", 13);

    REQUIRE(stmt.has_value());
    CHECK(stmt->find("SELECT owner_id,admin_id,staff FROM clinics") == 0);
    CHECK(stmt->find("13") != std::string::npos);
    CHECK(stmt->find("LIMIT 1;") != std::string::npos);
}

TEST_CASE("Service getSqlAddStaffStatement returns nullopt when data variant is wrong type", "[entities][sql][service]")
{
    // Constructing a Service with Create_t (not StaffData) makes getData() hold
    // the wrong variant — std::get<StaffData> throws and the method returns nullopt.
    const jsoncons::json  payload = makeJsonObject({{"name", "test"}});
    const Types::Create_t create(payload, 1);
    Service               svc(create, "clinics");
    pqxx::params params;
    CHECK_FALSE(svc.getSqlAddStaffStatement(params).has_value());
}

TEST_CASE("Service getSqlRemoveStaffStatement returns nullopt when data variant is wrong type", "[entities][sql][service]")
{
    const Types::Delete_t del(1);
    Service               svc(del, "pharmacies");
    pqxx::params params;
    CHECK_FALSE(svc.getSqlRemoveStaffStatement(params).has_value());
}

// ── Types direct construction ───────────────────────────────────────────────

TEST_CASE("Types::Create_t stores the payload and id", "[entities][types]")
{
    const jsoncons::json  payload = makeJsonObject({{"field", "value"}});
    const Types::Create_t create(payload, 77);

    REQUIRE(create.get_id().has_value());
    CHECK(create.get_id().value() == 77);
    CHECK(create.get_data().contains("field"));
}

TEST_CASE("Types::Read_t stores the schema set and id", "[entities][types]")
{
    const std::unordered_set<std::string> schema = {"name", "dob", "email"};
    const Types::Read_t                   read(schema, 55);

    REQUIRE(read.get_id().has_value());
    CHECK(read.get_id().value() == 55);
    CHECK(read.get_data().count("name") == 1);
    CHECK(read.get_data().count("email") == 1);
}

TEST_CASE("Types::Update_t stores the payload and id", "[entities][types]")
{
    const jsoncons::json  payload = makeJsonObject({{"status", "active"}});
    const Types::Update_t update(payload, 11);

    REQUIRE(update.get_id().has_value());
    CHECK(update.get_id().value() == 11);
    CHECK(update.get_data()["status"].as_string() == "active");
}

TEST_CASE("Types::Delete_t stores the id", "[entities][types]")
{
    const Types::Delete_t del(999);
    REQUIRE(del.get_id().has_value());
    CHECK(del.get_id().value() == 999);
}

TEST_CASE("Types::Data_t stores the id", "[entities][types]")
{
    const Types::Data_t data(123);
    REQUIRE(data.get_id().has_value());
    CHECK(data.get_id().value() == 123);
}

// PermissionManager Service_t canCreate is tested in test_permissions.cpp
