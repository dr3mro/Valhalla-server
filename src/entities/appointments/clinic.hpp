#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "entities/appointments/base/appointment.hpp"

class ClinicAppointment : public Appointment
{
   private:
    static constexpr auto TABLENAME = "clinics_appointments";
    static constexpr auto ORGNAME   = "clinics";

   public:
    template <typename T>
    explicit ClinicAppointment(const T& _data) : Appointment(_data, TABLENAME)
    {
    }

    ClinicAppointment()                                    = delete;
    ClinicAppointment(const ClinicAppointment&)            = delete;
    ClinicAppointment(ClinicAppointment&&)                 = delete;
    ClinicAppointment& operator=(ClinicAppointment&&)      = delete;
    ClinicAppointment& operator=(const ClinicAppointment&) = delete;

    static constexpr auto getTableName() { return TABLENAME; }
    static constexpr auto getOrgName() { return ORGNAME; }
    ~ClinicAppointment() override {};

    static std::optional<std::string> getServicePermissionsQuery(const std::string& service_name, std::uint64_t service_id)
    {
        return getServicePermissionsQueryImpl(service_name, service_id);
    }
};
