
#pragma once

#include "controllers/entitycontroller/entitycontroller.hpp"
#include "controllers/servicecontroller/servicecontrollerbase.hpp"

template <Service_t T>
class ServiceController : public EntityController<T>, public ServiceControllerBase
{
   public:
    explicit ServiceController() = default;

    virtual ~ServiceController() override = default;

    // CRUDS
    void Create(CALLBACK_&& callback, const Requester&& requester, std::string_view data) override;
    void Read(CALLBACK_&& callback, const Requester&& requester, std::string_view data) override;
    void Update(CALLBACK_&& callback, const Requester&& requester, std::string_view data, std::optional<uint64_t> id) override;
    void Delete(CALLBACK_&& callback, const Requester&& requester, std::optional<uint64_t> id) override;
    void Search(CALLBACK_&& callback, const Requester&& requester, std::string_view data) override;
};
