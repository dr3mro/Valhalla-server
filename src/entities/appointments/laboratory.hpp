#pragma once

#include "entities/appointments/base/appointment.hpp"

class LaboratoryAppointment : public Appointment
{
   private:
    static constexpr auto TABLENAME = "laboratories_appointments";
    static constexpr auto ORGNAME   = "laboratories";

   public:
    template <typename T>
    LaboratoryAppointment(const T &_data) : Appointment(_data, TABLENAME)
    {
    }
    static constexpr auto getTableName() { return TABLENAME; }
    static constexpr auto getOrgName() { return ORGNAME; }
    virtual ~LaboratoryAppointment() override = default;
    static std::optional<std::string> getServicePermissionsQuery(const std::string &service_name, std::uint64_t service_id)
    {
        return getServicePermissionsQueryImpl(service_name, service_id);
    }
};
