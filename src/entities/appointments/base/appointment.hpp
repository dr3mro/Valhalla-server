#pragma once
#include "entities/base/entity.hpp"

class Appointment : public Entity
{
   public:
    template <typename T>
    Appointment(const T &_data, const std::string &_tablename) : Entity(_data, _tablename)
    {
    }

    virtual ~Appointment() = default;

   protected:
    static std::optional<std::string> getServicePermissionsQueryImpl(const std::string &orgname, const std::string &service_name, std::uint64_t service_id)
    {
        return fmt::format(R"(
        SELECT
        c.owner_id,
        c.admin_id,
        c.staff,
        p.id AS patient_id,
        p.clinic_id AS clinic_id,
        x.clinic_id AS service_id
        FROM
        {} c
        LEFT JOIN patients p ON c.id = p.clinic_id
        LEFT JOIN {} x ON c.id = x.clinic_id
        WHERE
        x.id = {};)",
            orgname, service_name, service_id);
    }
};
