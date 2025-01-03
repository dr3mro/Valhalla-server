
#pragma once
#include <fmt/format.h>

#include <cstdint>
#include <jsoncons/json.hpp>
#include <memory>
#include <optional>
#include <string_view>

#include "controllers/base/controller/controller.hpp"
#include "controllers/entitycontroller/entitycontrollerbase.hpp"
#include "gatekeeper/gatekeeper.hpp"
#include "store/store.hpp"
#include "utils/global/callback.hpp"

using GateKeeper = api::v2::GateKeeper;

template <typename T>
class EntityController : public Controller, public EntityControllerBase
{
   public:
    EntityController()                                    = default;
    EntityController(const EntityController &)            = default;
    EntityController(EntityController &&)                 = default;
    EntityController &operator=(const EntityController &) = default;
    EntityController &operator=(EntityController &&)      = default;
    ~EntityController() override                          = default;
    // CRUDS

    void Create(CALLBACK_ &&callback, const Requester &&requester, std::string_view data) override;
    void Read(CALLBACK_ &&callback, const Requester &&requester, std::string_view data) override;
    void Update(CALLBACK_ &&callback, const Requester &&requester, std::string_view data, std::optional<uint64_t> _id) override;
    void Delete(CALLBACK_ &&callback, const Requester &&requester, std::optional<uint64_t> _id) override;
    void Search(CALLBACK_ &&callback, const Requester &&requester, std::string_view data) override;

   private:
    std::shared_ptr<GateKeeper> gateKeeper = Store::getObject<GateKeeper>();
};
