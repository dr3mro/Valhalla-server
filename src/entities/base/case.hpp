#pragma once

#include "entities/base/entity.hpp"
class Case : public Entity
{
   public:
    template <typename T>
    Case(const T &data, const std::string &tablename) : Entity(data, tablename)
    {
    }
    Case(const std::string &tablename) : Entity(tablename) {};

    virtual ~Case() = default;
};
