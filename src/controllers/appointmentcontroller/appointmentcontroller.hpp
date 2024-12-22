
#pragma once

#include "controllers/appointmentcontroller/appointmentcontrollerbase.hpp"
#include "controllers/entitycontroller/entitycontroller.hpp"

template <Appointment_t T>
class AppointmentController : public EntityController<T>, public AppointmentControllerBase
{
   public:
    explicit AppointmentController()                               = default;
    AppointmentController(AppointmentController&&)                 = default;
    AppointmentController(const AppointmentController&)            = default;
    AppointmentController& operator=(AppointmentController&&)      = default;
    AppointmentController& operator=(const AppointmentController&) = default;
    ~AppointmentController() override                              = default;

    // CRUDS
    void Create(CALLBACK_&& callback, const Requester&& requester, std::string_view data) override;
    void Read(CALLBACK_&& callback, const Requester&& requester, std::string_view data) override;
    void Update(CALLBACK_&& callback, const Requester&& requester, std::string_view data, std::optional<uint64_t> _id) override;
    void Delete(CALLBACK_&& callback, const Requester&& requester, std::optional<uint64_t> _id) override;
    void Search(CALLBACK_&& callback, const Requester&& requester, std::string_view data) override;
};
