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

    static std::optional<std::string> getPermissionsQueryForReadImpl(const std::string &org_name, const std::string &appointments_table, std::uint64_t appointment_id)
    {
        // org_name is plural (e.g. "clinics") but the FK column is singular (e.g. "clinic_id")
        std::string fk_col = org_name.back() == 's' ? org_name.substr(0, org_name.size() - 1) + "_id" : org_name + "_id";
        return fmt::format(R"(
        SELECT
        s.owner_id,
        s.admin_id,
        s.staff
        FROM
        {} s
        JOIN {} a ON a.{} = s.id
        WHERE
        a.id = {};)",
            org_name, appointments_table, fk_col, appointment_id);
    }
};
