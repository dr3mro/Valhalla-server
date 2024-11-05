#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/entity.hpp"

using json = jsoncons::json;

class PaidServices : public Entity
{
   private:
    static constexpr auto TABLENAME = "clinics_paidservices";

   public:
    PaidServices() : Entity(TABLENAME) {}

    template <typename T>
    PaidServices(const T &_data) : Entity(_data, TABLENAME)
    {
    }

    std::string getTableName() { return TABLENAME; }

    ~PaidServices() override = default;
};
