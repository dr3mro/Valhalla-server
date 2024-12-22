
#pragma once
#include <fmt/format.h>

#include <cstdint>
#include <jsoncons/json.hpp>
#include <memory>

#include "controllers/base/controller/controller.hpp"
#include "controllers/entitycontroller/entitycontrollerbase.hpp"
#include "gatekeeper/gatekeeper.hpp"

template <typename T>
class EntityController : public Controller, public EntityControllerBase
{
   public:
    EntityController()                                    = default;
    EntityController(const EntityController &)            = default;
    EntityController(EntityController &&)                 = default;
    EntityController &operator=(const EntityController &) = default;
    EntityController &operator=(EntityController &&)      = default;
    virtual ~EntityController()                           = default;
    // CRUDS

    void Create(CALLBACK_ &&callback, const Requester &&requester, std::string_view data) override;
    void Read(CALLBACK_ &&callback, const Requester &&requester, std::string_view data) override;
    void Update(CALLBACK_ &&callback, const Requester &&requester, std::string_view data, std::optional<uint64_t> _id) override;
    void Delete(CALLBACK_ &&callback, const Requester &&requester, std::optional<uint64_t> _id) override;
    void Search(CALLBACK_ &&callback, const Requester &&requester, std::string_view data) override;

   private:
    std::shared_ptr<GateKeeper> gateKeeper = Store::getObject<GateKeeper>();
};
