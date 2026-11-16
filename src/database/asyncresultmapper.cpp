#include "database/asyncresultmapper.hpp"
#include "utils/message/message.hpp"
#include <fmt/core.h>

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

            const auto sql_type = field.sqlType();

            const std::string name(field_name);
            const std::string value(field.c_str());

            if (name == "active")
            {
                object[name] = (value == "t" || value == "true" || value == "1");
            }
            else if (name == "id" || name.ends_with("_id") || name == "appointment_duration" || name == "appointment_payment_amount")
            {
                try
                {
                    object[name] = std::stoi(value);
                }
                catch (...)
                {
                    object[name] = value;
                }
            }
            else if (!value.empty() && (value.front() == '{' || value.front() == '['))
            {
                try
                {
                    object[name] = jsoncons::json::parse(value);
                }
                catch (...)
                {
                    object[name] = value;
                }
            }
            else
            {
                object[name] = value;
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
