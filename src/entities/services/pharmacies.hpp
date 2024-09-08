#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/entity.hpp"

using json = jsoncons::json;

class Pharmacies : public Entity
{
   private:
    static constexpr auto TABLENAME = "pharmacies";

   public:
    Pharmacies() : Entity(TABLENAME) {}

    template <typename T>
    Pharmacies(const T &_data) : Entity(_data, TABLENAME)
    {
    }
    std::string getTableName() { return TABLENAME; }
    ~Pharmacies() override = default;
};
