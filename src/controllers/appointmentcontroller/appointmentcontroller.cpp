

#include "controllers/appointmentcontroller/appointmentcontroller.hpp"

template <Appointment_t T>
void AppointmentController<T>::Create(CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
    EntityController<T>::Create(std::move(callback), std::move(requester), data);
}

template <Appointment_t T>
void AppointmentController<T>::Read(CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
    EntityController<T>::Read(std::move(callback), std::move(requester), data);
}

template <Appointment_t T>
void AppointmentController<T>::Update(CALLBACK_&& callback, const Requester&& requester, std::string_view data, const std::optional<uint64_t> _id)
{
    EntityController<T>::Update(std::move(callback), std::move(requester), data, _id);
}

template <Appointment_t T>
void AppointmentController<T>::Delete(CALLBACK_&& callback, const Requester&& requester, const std::optional<uint64_t> _id)
{
    EntityController<T>::Delete(std::move(callback), std::move(requester), _id);
}

template <Appointment_t T>
void AppointmentController<T>::Search(CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
    EntityController<T>::Search(std::move(callback), std::move(requester), data);
}
#include "entities/appointments/clinic.hpp"           // IWYU pragma: keep
#include "entities/appointments/laboratory.hpp"       // IWYU pragma: keep
#include "entities/appointments/pharmacy.hpp"         // IWYU pragma: keep
#include "entities/appointments/radiologycenter.hpp"  // IWYU pragma: keep

#define INSTANTIATE_APPOINTMENT_CONTROLLER(TYPE)                                                                                                              \
    template void AppointmentController<TYPE>::Create(CALLBACK_&& callback, const Requester&& requester, std::string_view data);                              \
    template void AppointmentController<TYPE>::Read(CALLBACK_&& callback, const Requester&& requester, std::string_view data);                                \
    template void AppointmentController<TYPE>::Update(CALLBACK_&& callback, const Requester&& requester, std::string_view data, std::optional<uint64_t> _id); \
    template void AppointmentController<TYPE>::Delete(CALLBACK_&& callback, const Requester&& requester, std::optional<uint64_t> _id);                        \
    template void AppointmentController<TYPE>::Search(CALLBACK_&& callback, const Requester&& requester, std::string_view data);

// Instantiate for all entity types

INSTANTIATE_APPOINTMENT_CONTROLLER(ClinicAppointment)
INSTANTIATE_APPOINTMENT_CONTROLLER(PharmacyAppointment)
INSTANTIATE_APPOINTMENT_CONTROLLER(LaboratoryAppointment)
INSTANTIATE_APPOINTMENT_CONTROLLER(RadiologyCenterAppointment)