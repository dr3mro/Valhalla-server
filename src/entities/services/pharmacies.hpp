#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/service.hpp"

using json = jsoncons::json;

class Pharmacies : public Service
{
   private:
    static constexpr auto TABLENAME = "pharmacies";

   public:
    Pharmacies() : Service(TABLENAME) {}

    template <typename T>
    Pharmacies(const T &_data) : Service(_data, TABLENAME)
    {
    }
    std::string getTableName() { return TABLENAME; }
    ~Pharmacies() override = default;
};
