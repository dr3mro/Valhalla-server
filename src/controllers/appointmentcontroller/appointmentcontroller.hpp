
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
    void Create(const crow::request &req, crow::response &res, const jsoncons::json &request_json) override;
    void Read(const crow::request &req, crow::response &res, const json &request_json) override;
    void Update(const crow::request &req, crow::response &res, const jsoncons::json &request_json) override;
    void Delete(const crow::request &req, crow::response &res, const std::unordered_map<std::string, std::string> &params) override;
    void Search(const crow::request &req, crow::response &res, const jsoncons::json &request_json) override;
};

#include "controllers/appointmentcontroller/appointmentcontroller.tpp"
