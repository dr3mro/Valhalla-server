
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
        RestHelper::failureResponse(std::ref(res), e.what());
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
            RestHelper::failureResponse(std::ref(res), e.what());
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
        json                       data(body);
        Entity::StaffData          staffData(data);
        std::optional<std::string> response;
        T                          service(staffData);
        if (staffData.parse_status && staffData.toInviteJson(data))
        {
            response =
                Communicate::sendRequest(email_sender_daemon_config_.host.data(), email_sender_daemon_config_.port,
                                         email_sender_daemon_config_.message_queue_path.data(), crow::HTTPMethod::POST, data.to_string().c_str());

            if (response.has_value())
            {
                RestHelper::successResponse(res, crow::status::OK, response.value());
            }
            else
            {
                RestHelper::errorResponse(res, crow::status::BAD_REQUEST, "Failed to send invite.");
            }
        }
        else
        {
            RestHelper::errorResponse(res, crow::status::BAD_REQUEST, "Failed to create invite json.");
        }
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(std::ref(res), e.what());
    }
}