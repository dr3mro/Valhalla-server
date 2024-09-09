
#pragma once
#include <fmt/format.h>

#include <functional>
#include <jsoncons/json.hpp>

#include "controllers/base/controller/controller.hpp"
#include "controllers/staffcontroller/staffcontrollerBase.hpp"
#include "entities/base/entity.hpp"
#include "utils/communicate/communicate.hpp"
#include "utils/resthelper/resthelper.hpp"
using json = jsoncons::json;

template <typename T>
class StaffController : public StaffControllerBase, public Controller
{
   public:
    explicit StaffController()  = default;
    ~StaffController() override = default;

    // CRUDS
    void AddStaffToEntity(const crow::request &req, crow::response &res, const jsoncons::json &body) override;
    void RemoveStaffFromEntity(const crow::request &req, crow::response &res, const jsoncons::json &body) override;
    void InviteStaffToEntity(const crow::request &req, crow::response &res, const jsoncons::json &body) override;

   private:
    T                               service;
    std::shared_ptr<Configurator>   cfg_                        = Store::getObject<Configurator>();
    Configurator::EmailSenderConfig email_sender_daemon_config_ = cfg_->get<Configurator::EmailSenderConfig>();
};

template <typename T>
void StaffController<T>::AddStaffToEntity(const crow::request &req, crow::response &res, const jsoncons::json &body)
{
    (void)req;
    json response;
    try
    {
        json              data(body);
        json              payload = data.at("payload");
        Entity::StaffData staffData(payload);

        T service(staffData);
        Controller::addStaff(std::ref(res), service);
    }
    catch (const std::exception &e)
    {
        RestHelper::sendErrorResponse(std::ref(res), std::ref(response), "Failure", fmt::format("Failed: {}", e.what()), -2, 500);
    }
}
template <typename T>
void StaffController<T>::RemoveStaffFromEntity(const crow::request &req, crow::response &res, const jsoncons::json &body)
{
    {
        (void)req;
        json response;
        try
        {
            json              data(body);
            json              payload = data.at("payload");
            Entity::StaffData staffData(payload);

            T service(staffData);
            Controller::removeStaff(std::ref(res), service);
        }
        catch (const std::exception &e)
        {
            RestHelper::sendErrorResponse(std::ref(res), std::ref(response), "Failure", fmt::format("Failed: {}", e.what()), -2, 500);
        }
    }
}
template <typename T>
void StaffController<T>::InviteStaffToEntity(const crow::request &req, crow::response &res, const jsoncons::json &body)
{
    (void)req;
    json response;
    try
    {
        json              data(body);
        json              payload = data.at("payload");
        Entity::StaffData staffData(payload);
        std::optional<std::string> response;
        T service(staffData);
        if (staffData.parse_status && staffData.toInviteJson(payload))
        {
            response = Communicate::sendRequest(email_sender_daemon_config_.host.data(), email_sender_daemon_config_.port,
                                                   email_sender_daemon_config_.message_queue_path.data(), crow::HTTPMethod::POST,
                                                   payload.to_string().c_str());
            res.code = 200;
            std::string reply = response ? response.value() : "Failed to send invite";
            res.write(std::format("{}", reply));
            res.end();
        }
        else
        {
            res.code = 400;
            res.write("Failed to create invite json");
            res.end();
        }
    }
    catch (const std::exception &e)
    {
        RestHelper::sendErrorResponse(std::ref(res), std::ref(response), "Failure", fmt::format("Failed: {}", e.what()), -2, 500);
    }
}