#pragma once

#include <fmt/core.h>

#include <cstdint>
#include <jsoncons/json.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "controllers/clientcontroller/clientcontrollerbase.hpp"
#include "controllers/entitycontroller/entitycontroller.hpp"
#include "controllers/entitycontroller/entitycontrollerbase.hpp"
#include "gatekeeper/gatekeeper.hpp"
#include "store/store.hpp"
#include "utils/global/callback.hpp"
#include "utils/global/concepts.hpp"
#include "utils/global/global.hpp"

template <Client_t T>
class ClientController : public EntityController<T>, public ClientControllerBase
{
   public:
    ClientController()                                   = default;
    ClientController(const ClientController&)            = default;
    ClientController(ClientController&&)                 = default;
    ClientController& operator=(const ClientController&) = default;
    ClientController& operator=(ClientController&&)      = default;
    ~ClientController() override                         = default;

    void Create(CALLBACK_&& callback, const Requester&& requester, std::string_view data) final;
    void Read(CALLBACK_&& callback, const Requester&& requester, std::string_view data) final;
    void Update(CALLBACK_&& callback, const Requester&& requester, std::string_view data, std::optional<uint64_t> _id) final;
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
