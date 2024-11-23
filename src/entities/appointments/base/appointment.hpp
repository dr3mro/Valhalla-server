#pragma once
#include "entities/base/entity.hpp"
#include "jsoncons/basic_json.hpp"
using json = jsoncons::json;

class Appointment : public Entity
{
   public:
    template <typename T>
    Appointment(const T &_data, const std::string &_tablename) : Entity(_data, _tablename)
    {
    }

    virtual ~Appointment() = default;
};
