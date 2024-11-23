
#pragma once

#include "controllers/appointmentcontroller/appointmentcontrollerbase.hpp"
#include "controllers/entitycontroller/entitycontroller.hpp"

template <typename T, typename CALLBACK>
class AppointmentController : public EntityController<T, CALLBACK>, public AppointmentControllerBase<CALLBACK>
{
   public:
    explicit AppointmentController()          = default;
    virtual ~AppointmentController() override = default;

    // CRUDS
    void Create(CALLBACK &&callback, std::string_view data) override;
    void Read(CALLBACK &&callback, std::string_view data) override;
    void Update(CALLBACK &&callback, std::string_view data, std::optional<uint64_t> id) override;
    void Delete(CALLBACK &&callback, std::optional<uint64_t> id) override;
    void Search(CALLBACK &&callback, std::string_view data) override;
};

template <typename T, typename CALLBACK>
void AppointmentController<T, CALLBACK>::Create(CALLBACK &&callback, std::string_view data)
{
    EntityController<T, CALLBACK>::Create(std::move(callback), data);
}

template <typename T, typename CALLBACK>
void AppointmentController<T, CALLBACK>::Read(CALLBACK &&callback, std::string_view data)
{
    EntityController<T, CALLBACK>::Read(std::move(callback), data);
}

template <typename T, typename CALLBACK>
void AppointmentController<T, CALLBACK>::Update(CALLBACK &&callback, std::string_view data, const std::optional<uint64_t> id)
{
    EntityController<T, CALLBACK>::Update(std::move(callback), data, id);
}

template <typename T, typename CALLBACK>
void AppointmentController<T, CALLBACK>::Delete(CALLBACK &&callback, const std::optional<uint64_t> id)
{
    EntityController<T, CALLBACK>::Delete(std::move(callback), id);
}

template <typename T, typename CALLBACK>
void AppointmentController<T, CALLBACK>::Search(CALLBACK &&callback, std::string_view data)
{
    EntityController<T, CALLBACK>::Search(std::move(callback), data);
}
