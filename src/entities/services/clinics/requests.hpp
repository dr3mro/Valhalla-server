#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/entity.hpp"

using json = jsoncons::json;

class Requests : public Entity
{
   private:
    static constexpr auto TABLENAME = "requests";

   public:
    Requests() : Entity(TABLENAME) {}

    template <typename T>
    Requests(const T &_data) : Entity(_data, TABLENAME)
    {
    }

    std::string getTableName() { return TABLENAME; }

    ~Requests() override = default;
};
