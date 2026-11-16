#include <catch2/catch_test_macros.hpp>
#include <stdexcept>

#include "database/asyncdberror.hpp"
#include "database/databasebusy.hpp"

TEST_CASE("error mapping maps DatabaseBusy to 503", "[database][async][error]")
{
    DatabaseBusy busy;
    CHECK(asyncdb::isBusy(busy));
    CHECK(asyncdb::toHttpStatus(busy) == drogon::k503ServiceUnavailable);
}

TEST_CASE("error mapping maps generic errors to 500", "[database][async][error]")
{
    std::runtime_error generic("boom");
    CHECK_FALSE(asyncdb::isBusy(generic));
    CHECK(asyncdb::toHttpStatus(generic) == drogon::k500InternalServerError);
}
