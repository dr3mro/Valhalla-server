
#pragma once

#include "controllers/appointmentcontroller/appointmentcontrollerbase.hpp"
#include "controllers/entitycontroller/entitycontroller.hpp"

template <Appointment_t T>
class AppointmentController : public EntityController<T>, public AppointmentControllerBase
{
   public:
    explicit AppointmentController()          = default;
    virtual ~AppointmentController() override = default;

    // CRUDS
    void Create(CALLBACK_&& callback, const Requester&& requester, std::string_view data) override;
    void Read(CALLBACK_&& callback, const Requester&& requester, std::string_view data) override;
    void Update(CALLBACK_&& callback, const Requester&& requester, std::string_view data, std::optional<uint64_t> id) override;
    void Delete(CALLBACK_&& callback, const Requester&& requester, std::optional<uint64_t> id) override;
    void Search(CALLBACK_&& callback, const Requester&& requester, std::string_view data) override;
};
