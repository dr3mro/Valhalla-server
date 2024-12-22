#pragma once

#include <cstdint>
#include <jsoncons/json.hpp>
#include <optional>
#include <string>

#include "entities/appointments/base/appointment.hpp"

class RadiologyCenterAppointment : public Appointment
{
   private:
    static constexpr auto TABLENAME = "radiologycenters_appointments";
    static constexpr auto ORGNAME   = "radiologycenters";

   public:
    template <typename T>
    explicit RadiologyCenterAppointment(const T &_data) : Appointment(_data, TABLENAME)
    {
    }

    RadiologyCenterAppointment(const RadiologyCenterAppointment &)            = delete;
    RadiologyCenterAppointment(RadiologyCenterAppointment &&)                 = delete;
    RadiologyCenterAppointment &operator=(const RadiologyCenterAppointment &) = delete;
    RadiologyCenterAppointment &operator=(RadiologyCenterAppointment &&)      = delete;

    static constexpr auto getTableName() { return TABLENAME; }
    static constexpr auto getOrgName() { return ORGNAME; }

    ~RadiologyCenterAppointment() override = default;

    static std::optional<std::string> getServicePermissionsQuery(const std::string &service_name, std::uint64_t service_id)
    {
        return getServicePermissionsQueryImpl(service_name, service_id);
    }
};
