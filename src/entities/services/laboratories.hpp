#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/service.hpp"

class Laboratories : public Service
{
   private:
    static constexpr auto TABLENAME = "laboratories";

   public:
    // Laboratories() : Service(TABLENAME) {}

    template <typename T>
    Laboratories(const T& _data) : Service(_data, TABLENAME)
    {
    }
    static constexpr auto getTableName() { return TABLENAME; }
    ~Laboratories() override = default;
};
