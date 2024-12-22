#pragma once
#include <fmt/core.h>

#include <cstdint>
#include <optional>
#include <string>

#include "entities/base/entity.hpp"

class Appointment : public Entity
{
   public:
    template <typename T>
    explicit Appointment(const T &_data, const std::string &_tablename) : Entity(_data, _tablename)
    {
    }

    Appointment(const Appointment &)            = delete;
    Appointment(Appointment &&)                 = delete;
    Appointment &operator=(const Appointment &) = delete;
    Appointment &operator=(Appointment &&)      = delete;

    ~Appointment() override = default;

   protected:
    static std::optional<std::string> getServicePermissionsQueryImpl(const std::string &service_name, std::uint64_t service_id)
    {
        return fmt::format(R"(
        SELECT
        owner_id,
        admin_id,
        staff
        FROM
        {}
        WHERE
        id = {};)",
            service_name, service_id);
    }
};
