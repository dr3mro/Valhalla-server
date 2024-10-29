#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/entity.hpp"

using json = jsoncons::json;

class VisitDrugs : public Entity
{
   private:
    static constexpr auto TABLENAME = "visitdrugs";

   public:
    VisitDrugs() : Entity(TABLENAME) {}

    template <typename T>
    VisitDrugs(const T &_data) : Entity(_data, TABLENAME)
    {
    }

    std::string getTableName() { return TABLENAME; }

    ~VisitDrugs() override = default;
};
