#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/entity.hpp"

using json = jsoncons::json;

class RadiologyCenters : public Entity
{
   private:
   private:
    static constexpr auto TABLENAME = "radiologycenters";

   public:
    RadiologyCenters() : Entity(TABLENAME) {}

    template <typename T>
    RadiologyCenters(const T &_data) : Entity(_data, TABLENAME)
    {
    }
    std::string getTableName() { return TABLENAME; }
    ~RadiologyCenters() override = default;
};
