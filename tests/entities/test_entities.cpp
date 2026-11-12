#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <unordered_set>

#include <jsoncons/json.hpp>

#include "entities/base/entity.hpp"
#include "entities/base/types.hpp"
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
    const auto statement = entity.getSqlCreateStatement();

    REQUIRE(statement.has_value());
    // jsoncons uses a sorted policy, so keys are ordered alphabetically; the
    // id column is appended into the payload by the statement builder.
    CHECK(statement->find("INSERT INTO clinics (") == 0);
    CHECK(statement->find("description,id,name") != std::string::npos);
    CHECK(statement->find("VALUES ($1,$2,$3)") != std::string::npos);
    CHECK(statement->back() == ';');
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
    const auto statement = entity.getSqlUpdateStatement();

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
    const auto statement = entity.getSqlSearchStatement();

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
