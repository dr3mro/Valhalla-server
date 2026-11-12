#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <unordered_set>

#include <jsoncons/json.hpp>

#include "entities/base/client.hpp"
#include "entities/base/entity.hpp"
#include "entities/base/types.hpp"
#include "entities/people/provider.hpp"
#include "entities/people/user.hpp"

using namespace api::v2;

namespace
{
    jsoncons::json makeJsonObject(std::initializer_list<std::pair<const char *, std::string>> items)
    {
        jsoncons::json object = jsoncons::json::object();
        for (const auto &[key, value] : items)
        {
            object[key] = value;
        }
        return object;
    }
}  // namespace

TEST_CASE("Entity create statement lists every column with placeholders", "[entities][sql]")
{
    const jsoncons::json payload = makeJsonObject({{"name", "clinic"}, {"description", "general"}});
    const Types::Create_t create(payload, 5);

    Entity     entity(create, "clinics");
    pqxx::params params;
    const auto   statement = entity.getSqlCreateStatementParameterized(params);

    REQUIRE(statement.has_value());
    // jsoncons uses a sorted policy, so keys are ordered alphabetically; the
    // id column is appended into the payload by the statement builder.
    CHECK(statement->find("INSERT INTO clinics (") == 0);
    CHECK(statement->find("description,id,name") != std::string::npos);
    CHECK(statement->find("VALUES ($1,$2,$3)") != std::string::npos);
    CHECK(statement->back() == ';');
}

TEST_CASE("Entity plain statements inline the values for the injection check", "[entities][sql]")
{
    const jsoncons::json payload = makeJsonObject({{"name", "renamed"}});
    const Types::Update_t update(payload, 12);

    Entity     entity(update, "users");
    const auto statement = entity.getSqlUpdateStatement();

    REQUIRE(statement.has_value());
    CHECK(statement->find("name = 'renamed'") != std::string::npos);
    CHECK(statement->find('$') == std::string::npos);
}

TEST_CASE("Entity read statement targets the _safe view", "[entities][sql]")
{
    const std::unordered_set<std::string> schema = {"name", "dob"};
    const Types::Read_t                   read(schema, 9);

    Entity     entity(read, "users");
    const auto statement = entity.getSqlReadStatement();

    REQUIRE(statement.has_value());
    CHECK(statement->find("SELECT ") == 0);
    CHECK(statement->find(" FROM users_safe WHERE id=9 LIMIT 1;") != std::string::npos);
    CHECK(statement->find("name") != std::string::npos);
    CHECK(statement->find("dob") != std::string::npos);
}

TEST_CASE("Entity read statement with an empty schema selects all columns", "[entities][sql]")
{
    const Types::Read_t read({}, 3);

    Entity     entity(read, "users");
    const auto statement = entity.getSqlReadStatement();

    REQUIRE(statement.has_value());
    CHECK(statement->find("SELECT * FROM users_safe WHERE id=3 LIMIT 1;") == 0);
}

TEST_CASE("Entity update statement builds a SET clause with parameters", "[entities][sql]")
{
    const jsoncons::json payload = makeJsonObject({{"name", "renamed"}});
    const Types::Update_t update(payload, 12);

    Entity     entity(update, "users");
    pqxx::params params;
    const auto   statement = entity.getSqlUpdateStatementParameterized(params);

    REQUIRE(statement.has_value());
    CHECK(statement->find("UPDATE users SET") == 0);
    CHECK(statement->find("name = $1") != std::string::npos);
    CHECK(statement->find("WHERE id=12 RETURNING *;") != std::string::npos);
}

TEST_CASE("Entity delete statement removes by id", "[entities][sql]")
{
    const Types::Delete_t del(21);

    Entity     entity(del, "clinics");
    const auto statement = entity.getSqlDeleteStatement();

    REQUIRE(statement.has_value());
    CHECK(*statement == "DELETE FROM clinics where id=21 returning id;");
}

TEST_CASE("Entity search statement parameterises the keyword and honours paging", "[entities][sql]")
{
    const jsoncons::json search_j = jsoncons::json::parse(R"({
        "keyword": "fever",
        "filter": "name",
        "order_by": "created_at",
        "direction": 0,
        "limit": 10,
        "offset": 20
    })");
    bool            success = false;
    const Types::Search_t search(search_j, success);
    REQUIRE(success);

    Entity     entity(search, "patients");
    pqxx::params params;
    const auto   statement = entity.getSqlSearchStatementParameterized(params);

    REQUIRE(statement.has_value());
    CHECK(statement->find("SELECT * FROM patients_safe WHERE name::text ILIKE $1") != std::string::npos);
    CHECK(statement->find("ORDER BY created_at ASC") != std::string::npos);
    // LIMIT is limit + 1 (the extra row detects a next page).
    CHECK(statement->find("LIMIT 11 OFFSET 20;") != std::string::npos);
}

TEST_CASE("Entity exposes its group name and data variant", "[entities][sql]")
{
    const Types::Delete_t del(1);
    const Entity          entity(del, "users");

    CHECK(entity.getGroupName() == "users");
    CHECK_NOTHROW(std::get<Types::Delete_t>(entity.getData()));
}

TEST_CASE("User builds its get-services query", "[entities][sql]")
{
    const Types::Data_t data(33);
    User       user(data);
    const auto statement = user.getSqlGetServicesStatement();
    REQUIRE(statement.has_value());
    CHECK(*statement == "SELECT id from users WHERE id = 33");
}

TEST_CASE("Provider builds its get-services query", "[entities][sql]")
{
    const Types::Data_t data(7);
    Provider   provider(data);
    const auto statement = provider.getSqlGetServicesStatement();
    REQUIRE(statement.has_value());
    CHECK(statement->find("7") != std::string::npos);
    CHECK(statement->find("clinics") != std::string::npos);
    CHECK(statement->find("laboratories") != std::string::npos);
    CHECK(statement->find("pharmacies") != std::string::npos);
    CHECK(statement->find("radiologycenters") != std::string::npos);
}

TEST_CASE("Client getSqlSuspendStatement builds an UPDATE that sets active=false", "[entities][sql]")
{
    const Types::SuspendData suspendData(55);
    User   user(suspendData);
    const auto statement = user.getSqlSuspendStatement();
    REQUIRE(statement.has_value());
    CHECK(statement->find("UPDATE users SET active=false") != std::string::npos);
    CHECK(statement->find("where id=55") != std::string::npos);
    CHECK(statement->find("returning id,active;") != std::string::npos);
}

TEST_CASE("Client getSqlActivateStatement builds an UPDATE that sets active=true", "[entities][sql]")
{
    const Types::SuspendData suspendData(77);
    User   user(suspendData);
    const auto statement = user.getSqlActivateStatement();
    REQUIRE(statement.has_value());
    CHECK(statement->find("UPDATE users SET active=true") != std::string::npos);
    CHECK(statement->find("where id=77") != std::string::npos);
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
    invite["some_field"] = "initial";

    const bool ok = sd.toInviteJson(invite);
    CHECK(ok);
    CHECK(invite.contains("subject"));
    CHECK(invite.contains("template"));
    CHECK(invite.contains("link"));
    CHECK(invite.contains("project_name"));
    CHECK(invite.contains("generate_body"));
}

TEST_CASE("Entity getSqlCreateStatement inlines values (plain binder)", "[entities][sql]")
{
    const jsoncons::json payload = makeJsonObject({{"title", "intro"}});
    const Types::Create_t create(payload, 8);

    Entity     entity(create, "courses");
    const auto statement = entity.getSqlCreateStatement();

    REQUIRE(statement.has_value());
    CHECK(statement->find("INSERT INTO courses") == 0);
    CHECK(statement->find("'intro'") != std::string::npos);
    CHECK(statement->find('$') == std::string::npos);
}

TEST_CASE("Entity getSqlReadStatementParameterized returns the same query as the plain variant", "[entities][sql]")
{
    // The read/delete parameterized variants embed the id inline (same as the
    // plain variant) rather than using $N placeholders. They exist for API
    // uniformity; params is cleared but no bindings are added.
    const std::unordered_set<std::string> schema = {"name"};
    const Types::Read_t                   read(schema, 15);

    Entity       entity(read, "clinics");
    pqxx::params params;
    const auto   statement = entity.getSqlReadStatementParameterized(params);

    REQUIRE(statement.has_value());
    CHECK(statement->find("FROM clinics_safe WHERE id=15 LIMIT 1;") != std::string::npos);
    CHECK(statement->find("name") != std::string::npos);
}

TEST_CASE("Entity getSqlDeleteStatementParameterized returns the same query as the plain variant", "[entities][sql]")
{
    const Types::Delete_t del(99);
    Entity       entity(del, "appointments");
    pqxx::params params;
    const auto   statement = entity.getSqlDeleteStatementParameterized(params);

    REQUIRE(statement.has_value());
    CHECK(*statement == "DELETE FROM appointments where id=99 returning id;");
}

TEST_CASE("Entity getSqlSearchStatement inlines the keyword", "[entities][sql]")
{
    const jsoncons::json search_j = jsoncons::json::parse(R"({
        "keyword": "headache", "filter": "symptom",
        "order_by": "id", "direction": 1, "limit": 5, "offset": 0
    })");
    bool success = false;
    const Types::Search_t search(search_j, success);
    REQUIRE(success);

    Entity     entity(search, "cases");
    const auto statement = entity.getSqlSearchStatement();

    REQUIRE(statement.has_value());
    CHECK(statement->find("headache") != std::string::npos);
    CHECK(statement->find("ORDER BY id DESC") != std::string::npos);
    CHECK(statement->find("LIMIT 6 OFFSET 0;") != std::string::npos);
}
