#pragma once

#include <fmt/core.h>

#include <jsoncons/json.hpp>
#include <memory>
#include <optional>

#include "controllers/clientcontroller/clientcontrollerbase.hpp"
#include "controllers/entitycontroller/entitycontroller.hpp"
#include "gatekeeper/gatekeeper.hpp"
#include "utils/global/global.hpp"
#include "utils/global/http.hpp"
#include "utils/global/requester.hpp"
#include "utils/jsonhelper/jsonhelper.hpp"

template <Client_t T>
class ClientController : public EntityController<T>, public ClientControllerBase
{
   public:
    ClientController()          = default;
    virtual ~ClientController() = default;

    void Create(CALLBACK_&& callback, const Requester&& requester, std::string_view data) final;
    void Read(CALLBACK_&& callback, const Requester&& requester, std::string_view data) final;
    void Update(CALLBACK_&& callback, const Requester&& requester, std::string_view data, std::optional<uint64_t> id) final;
    void Delete(CALLBACK_&& callback, const Requester&& requester, std::optional<uint64_t> client_id) final;
    void Search(CALLBACK_&& callback, const Requester&& requester, std::string_view data) final;
    void Login(CALLBACK_&& callback, const Requester&& requester, std::string_view data, const std::string& ip_address) final;
    void Logout(CALLBACK_&& callback, const Requester&& requester, const std::optional<std::string>& token, const std::string& ip_address) final;
    void Suspend(CALLBACK_&& callback, const Requester&& requester, std::optional<uint64_t> client_id) final;
    void Activate(CALLBACK_&& callback, const Requester&& requester, std::optional<uint64_t> client_id) final;
    void ResetPassword(CALLBACK_&& callback, const Requester&& requester, std::string_view data) final;
    void GetServices(CALLBACK_&& callback, const Requester&& requester, std::optional<uint64_t> client_id) final;

   private:
    std::shared_ptr<GateKeeper> gateKeeper = Store::getObject<GateKeeper>();
};
