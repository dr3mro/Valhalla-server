#include <drogon/orm/DbClient.h>

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>

#include "configurator/configurator.hpp"
#include "database/asyncdbclient.hpp"
#include "store/store.hpp"

TEST_CASE("AsyncDbClient keeps the client it is given", "[database][asyncdb]")
{
    AsyncDbClient empty(nullptr);
    CHECK(empty.client() == nullptr);
}

TEST_CASE("AsyncDbClient replaces the registered singleton in tests", "[database][asyncdb]")
{
    auto original = Store::getObject<AsyncDbClient>(nullptr);
    auto fake     = std::make_shared<AsyncDbClient>(nullptr);
    Store::replaceObject<AsyncDbClient>(fake);
    CHECK(Store::getObject<AsyncDbClient>(nullptr) == fake);
    Store::replaceObject<AsyncDbClient>(original);
}

TEST_CASE("AsyncDbClient connection string carries the DB_* settings", "[database][asyncdb]")
{
    Configurator::DatabaseConfig config;
    config.host = "db.example";
    config.port = 6543;
    config.name = "valhalla";
    config.user = "app";
    config.pass = "secret";

    const std::string text = AsyncDbClient::connectionString(config);
    CHECK(text.find("host=db.example") != std::string::npos);
    CHECK(text.find("port=6543") != std::string::npos);
    CHECK(text.find("dbname=valhalla") != std::string::npos);
    CHECK(text.find("user=app") != std::string::npos);
    CHECK(text.find("password=secret") != std::string::npos);
}

TEST_CASE("AsyncDbClient connection string honours DB_SSL", "[database][asyncdb]")
{
    Configurator::DatabaseConfig config;
    config.host = "db.example";
    config.ssl  = true;

    CHECK(AsyncDbClient::connectionString(config).find("sslmode=require") != std::string::npos);

    config.ssl = false;
    CHECK(AsyncDbClient::connectionString(config).find("sslmode=disable") != std::string::npos);
}
