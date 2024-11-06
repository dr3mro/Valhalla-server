#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/service.hpp"
using json = jsoncons::json;

class Clinics : public Service
{
   private:
    static constexpr auto TABLENAME = "clinics";

   public:
    template <typename T>
    Clinics(const T &_data) : Service(_data, TABLENAME)
    {
    }
    Clinics() : Service(TABLENAME) {}

    std::string getTableName() { return TABLENAME; }

    virtual ~Clinics() = default;
};
