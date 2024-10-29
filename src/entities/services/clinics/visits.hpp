#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/entity.hpp"

using json = jsoncons::json;

class Visits : public Entity
{
   private:
    static constexpr auto TABLENAME = "visits";

   public:
    Visits() : Entity(TABLENAME) {}

    template <typename T>
    Visits(const T &_data) : Entity(_data, TABLENAME)
    {
    }

    std::string getTableName() { return TABLENAME; }

    ~Visits() override = default;
};
