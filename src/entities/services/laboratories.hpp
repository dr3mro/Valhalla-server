#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/entity.hpp"

using json = jsoncons::json;

class Laboratories : public Entity
{
   private:
    static constexpr auto TABLENAME = "laboratories";

   public:
    Laboratories() : Entity(TABLENAME) {}

    template <typename T>
    Laboratories(const T& _data) : Entity(_data, TABLENAME)
    {
    }
    std::string getTableName() { return TABLENAME; }
    ~Laboratories() override = default;
};
