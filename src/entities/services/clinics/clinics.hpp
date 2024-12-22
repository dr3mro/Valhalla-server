#pragma once

#include "entities/base/service.hpp"

class Clinics : public Service
{
   private:
    static constexpr auto TABLENAME = "clinics";

   public:
    template <typename T>
    Clinics(const T &_data) : Service(_data, TABLENAME)
    {
    }

    static constexpr auto getTableName() { return TABLENAME; }

    ~Clinics() override = default;
};
