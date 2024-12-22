#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "entities/appointments/base/appointment.hpp"

class PharmacyAppointment : public Appointment
{
   private:
    static constexpr auto TABLENAME = "pharmacies_appointments";
    static constexpr auto ORGNAME   = "pharmacies";

   public:
    template <typename T>
    explicit PharmacyAppointment(const T &_data) : Appointment(_data, TABLENAME)
    {
    }
    PharmacyAppointment(const PharmacyAppointment &)            = delete;
    PharmacyAppointment(PharmacyAppointment &&)                 = delete;
    PharmacyAppointment &operator=(const PharmacyAppointment &) = delete;
    PharmacyAppointment &operator=(PharmacyAppointment &&)      = delete;

    static constexpr auto getTableName() { return TABLENAME; }
    static constexpr auto getOrgName() { return ORGNAME; }

    ~PharmacyAppointment() override = default;

    static std::optional<std::string> getServicePermissionsQuery(const std::string &service_name, std::uint64_t service_id)
    {
        return getServicePermissionsQueryImpl(service_name, service_id);
    }
};
