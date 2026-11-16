#include "database/asyncresultmapper.hpp"

#include <drogon/orm/Field.h>
#include <drogon/orm/Result.h>
#include <drogon/orm/Row.h>

#include <jsoncons/basic_json.hpp>
#include <jsoncons/json.hpp>

#include <cstddef>
#include <string>

namespace asyncdb
{
    jsoncons::json mapRow(const drogon::orm::Result& result, drogon::orm::Result::SizeType row)
    {
        jsoncons::json object;

        const auto column_count = result.columns();
        for (drogon::orm::Result::RowSizeType i = 0; i < column_count; ++i)
        {
            const std::string_view field_name = result.columnName(i);
            const auto             field      = result[row][i];

            if (field.isNull())
            {
                object[std::string(field_name)] = nullptr;
                continue;
            }

            const std::string type_name = result.getNativeTypeName(i);

            if (type_name == "int4")
            {
                object[std::string(field_name)] = field.as<int>();
            }
            else if (type_name == "bool")
            {
                object[std::string(field_name)] = field.as<bool>();
            }
            else if (type_name == "json" || type_name == "jsonb")
            {
                object[std::string(field_name)] = jsoncons::json::parse(field.as<std::string>());
            }
            else
            {
                object[std::string(field_name)] = std::string(field.c_str());
            }
        }

        return object;
    }

    jsoncons::json mapResultToObject(const drogon::orm::Result& result)
    {
        if (result.empty())
        {
            return jsoncons::json();
        }
        return mapRow(result, 0);
    }

    jsoncons::json::array mapResultToArray(const drogon::orm::Result& result)
    {
        jsoncons::json::array reply;

        for (drogon::orm::Result::SizeType row = 0; row < result.size(); ++row)
        {
            reply.push_back(mapRow(result, row));
        }
        return reply;
    }
}  // namespace asyncdb
