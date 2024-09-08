
#pragma once
#include <fmt/format.h>

#include <functional>
#include <jsoncons/json.hpp>

#include "controllers/base/controller/controller.hpp"
#include "entities/base/entity.hpp"
#include "utils/resthelper/resthelper.hpp"

using json = jsoncons::json;

template <typename T>
class StaffController : public Controller
{
   public:
    explicit StaffController()  = default;
    ~StaffController() override = default;

    // CRUDS
    void AddStaffToEntity(const crow::request &req, crow::response &res, const jsoncons::json &body);
    void RemoveStaffFromEntity(const crow::request &req, crow::response &res, const jsoncons::json &body);
    void InviteStaffToEntity(const crow::request &req, crow::response &res, const jsoncons::json &body);

   private:
    T                               service;
    std::shared_ptr<Configurator>   cfg_                        = Store::getObject<Configurator>();
    Configurator::EmailSenderConfig email_sender_daemon_config_ = cfg_->get<Configurator::EmailSenderConfig>();
};
