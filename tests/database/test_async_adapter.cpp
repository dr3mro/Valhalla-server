#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>
#include <vector>

#include <pqxx/pqxx>

#include "database/asyncsqlbinder.hpp"

TEST_CASE("adapter converts pqxx params to text params in order", "[database][async][adapter]")
{
    pqxx::params params;
    params.append(42);
    params.append(std::string("hello"));
    params.append(nullptr);

    const auto values = asyncdb::toTextParams(params);

    REQUIRE(values.size() == 3);
    REQUIRE(values[0].has_value());
    CHECK(values[0].value() == "42");
    REQUIRE(values[1].has_value());
    CHECK(values[1].value() == "hello");
    CHECK_FALSE(values[2].has_value());
}

TEST_CASE("adapter handles an empty parameter list", "[database][async][adapter]")
{
    pqxx::params params;
    const auto   values = asyncdb::toTextParams(params);
    CHECK(values.empty());
}
