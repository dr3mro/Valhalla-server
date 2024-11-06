#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/service.hpp"

using json = jsoncons::json;

class Laboratories : public Service
{
   private:
    static constexpr auto TABLENAME = "laboratories";

   public:
    Laboratories() : Service(TABLENAME) {}

    template <typename T>
    Laboratories(const T& _data) : Service(_data, TABLENAME)
    {
    }
    std::string getTableName() { return TABLENAME; }
    virtual ~Laboratories() = default;
};
