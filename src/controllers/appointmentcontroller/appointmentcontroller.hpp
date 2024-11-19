
#pragma once

#include "controllers/appointmentcontroller/appointmentcontrollerbase.hpp"
#include "controllers/entitycontroller/entitycontroller.hpp"

using json = jsoncons::json;

template <typename T>
class AppointmentController : public EntityController<T>, public AppointmentControllerBase
{
   public:
    explicit AppointmentController() = default;

    virtual ~AppointmentController() override = default;

    // CRUDS
    void Create(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data) override;
    void Read(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data) override;
    void Update(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data) override;
    void Delete(std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::unordered_map<std::string, std::string>& params) override;
    void Search(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data) override;
};

#include "controllers/appointmentcontroller/appointmentcontroller.tpp"
