
#pragma once

#include <memory>
#include <string_view>

#include "controllers/base/controller/controller.hpp"
#include "controllers/staffcontroller/staffcontrollerBase.hpp"
#include "utils/global/callback.hpp"

namespace api::v2
{
    class GateKeeper;
}  // namespace api::v2

class Configurator;

template <typename T>
class StaffController : public StaffControllerBase, public Controller
{
   public:
    StaffController();
    StaffController(const StaffController &)            = delete;
    StaffController(StaffController &&)                 = delete;
    StaffController &operator=(const StaffController &) = delete;
    StaffController &operator=(StaffController &&)      = delete;
    ~StaffController() override                         = default;

    // CRUDS
    void AddStaffToEntity(CALLBACK_ &&callback, const Requester &&requester, std::string_view data) override;
    void RemoveStaffFromEntity(CALLBACK_ &&callback, const Requester &&requester, std::string_view data) override;
    void InviteStaffToEntity(CALLBACK_ &&callback, const Requester &&requester, std::string_view data) override;

   private:
    std::shared_ptr<Configurator>   cfg_;
    Configurator::EmailSenderConfig email_sender_daemon_config_;
    std::shared_ptr<GateKeeper>     gateKeeper;
};
