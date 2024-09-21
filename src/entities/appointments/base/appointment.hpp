#pragma once

#include <any>

#include "entities/base/entity.hpp"
#include "fmt/format.h"
#include "jsoncons/basic_json.hpp"
using json = jsoncons::json;

class Appointment : public Entity
{
   public:
    template <typename T>
    Appointment(const T &_data, const std::string &_tablename) : Entity(_data, _tablename)
    {
    }

    Appointment(const std::string &_tablename) : Entity(_tablename) {}

    ~Appointment() = default;

    std::optional<std::string> getSqlSearchStatement() final
    {
        std::optional<std::string> query;
        try
        {
            SearchData searchdata = std::any_cast<SearchData>(getData());
            query = fmt::format("SELECT * FROM {}_safe WHERE {} = {} ORDER BY {} {} LIMIT {} OFFSET {};", tablename, searchdata.filter,
                                searchdata.keyword, searchdata.order_by, searchdata.direction, searchdata.limit + 1, searchdata.offset);
            std::cout << query.value() << '\n';
        }
        catch (const std::exception &e)
        {
            std::cerr << "faild to create query for search " << tablename << e.what() << '\n';
            return std::nullopt;
        }

        return query;
    }
};
