#pragma once

#include <drogon/orm/Result.h>

#include <jsoncons/basic_json.hpp>
#include <jsoncons/json.hpp>
#include <optional>
#include <string>

// Maps a Drogon PostgreSQL result set to jsoncons::json using the same type
// rules as Database::executeQuery (src/database/database.cpp): nulls stay null,
// text/varchar -> string, int4 -> number, bool -> boolean, json/jsonb -> parsed
// JSON, anything else -> its string form.
namespace asyncdb
{
    static constexpr unsigned int PG_TEXT_OID    = 1043;  // varchar (treated as text)
    static constexpr unsigned int PG_INTEGER_OID = 23;
    static constexpr unsigned int PG_BOOLEAN_OID = 16;
    static constexpr unsigned int PG_JSON_OID    = 114;
    static constexpr unsigned int PG_JSONB_OID   = 3802;

    // Map a single row (by index) of `result` into a JSON object.
    [[nodiscard]] jsoncons::json mapRow(const drogon::orm::Result& result, drogon::orm::Result::SizeType row);

    // Map the first row of `result` into a JSON object, or a null JSON value
    // when the result set is empty.
    [[nodiscard]] jsoncons::json mapResultToObject(const drogon::orm::Result& result);

    // Map every row of `result` into a JSON array.
    [[nodiscard]] jsoncons::json::array mapResultToArray(const drogon::orm::Result& result);
}  // namespace asyncdb

// Aliases for the Phase 2 vocabulary.
namespace ResultMapper        = asyncdb;
namespace AsyncDbResultMapper = asyncdb;
namespace DbResultMapper      = asyncdb;
