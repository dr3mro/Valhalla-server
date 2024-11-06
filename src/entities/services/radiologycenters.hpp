#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/service.hpp"

using json = jsoncons::json;

class RadiologyCenters : public Service
{
   private:
   private:
    static constexpr auto TABLENAME = "radiologycenters";

   public:
    RadiologyCenters() : Service(TABLENAME) {}

    template <typename T>
    RadiologyCenters(const T &_data) : Service(_data, TABLENAME)
    {
    }
    std::string getTableName() { return TABLENAME; }
    virtual ~RadiologyCenters() override = default;
};
