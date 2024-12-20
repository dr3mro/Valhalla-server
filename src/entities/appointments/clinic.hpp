#pragma once

#include "entities/appointments/base/appointment.hpp"

class ClinicAppointment : public Appointment
{
   private:
    static constexpr auto TABLENAME = "clinics_appointments";
    static constexpr auto ORGNAME   = "clinics";

   public:
    template <typename T>
    ClinicAppointment(const T &_data) : Appointment(_data, TABLENAME)
    {
    }
    static constexpr auto getTableName() { return TABLENAME; }
    static constexpr auto getOrgName() { return ORGNAME; }
    virtual ~ClinicAppointment() override = default;

    static std::optional<std::string> getServicePermissionsQuery(const std::string &service_name, std::uint64_t service_id)
    {
        return getServicePermissionsQueryImpl(service_name, service_id);
    }
};
