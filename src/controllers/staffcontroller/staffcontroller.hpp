
#pragma once

#include "controllers/base/controller/controller.hpp"
#include "controllers/staffcontroller/staffcontrollerBase.hpp"
#include "gatekeeper/gatekeeper.hpp"

template <typename T>
class StaffController : public StaffControllerBase, public Controller
{
   public:
    StaffController() : cfg_(Store::getObject<Configurator>()), email_sender_daemon_config_(cfg_->get<Configurator::EmailSenderConfig>()) {}
    virtual ~StaffController() override = default;

    // CRUDS
    void AddStaffToEntity(CALLBACK_ &&callback, const Requester &&requester, std::string_view data) override;
    void RemoveStaffFromEntity(CALLBACK_ &&callback, const Requester &&requester, std::string_view data) override;
    void InviteStaffToEntity(CALLBACK_ &&callback, const Requester &&requester, std::string_view data) override;

   private:
    std::shared_ptr<Configurator>   cfg_;
    Configurator::EmailSenderConfig email_sender_daemon_config_;
    std::shared_ptr<GateKeeper>     gateKeeper = Store::getObject<GateKeeper>();
};
