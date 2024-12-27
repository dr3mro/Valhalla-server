
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "controllers/entitycontroller/entitycontroller.hpp"
#include "controllers/entitycontroller/entitycontrollerbase.hpp"
#include "controllers/servicecontroller/servicecontrollerbase.hpp"
#include "utils/global/callback.hpp"
#include "utils/global/concepts.hpp"

template <Service_t T>
class ServiceController : public EntityController<T>, public ServiceControllerBase
{
   public:
    explicit ServiceController() = default;

    ServiceController(const ServiceController&)            = delete;
    ServiceController(ServiceController&&)                 = delete;
    ServiceController& operator=(const ServiceController&) = delete;
    ServiceController& operator=(ServiceController&&)      = delete;
    ~ServiceController() override                          = default;

    // CRUDS
    void Create(CALLBACK_&& callback, const Requester&& requester, std::string_view data) override;
    void Read(CALLBACK_&& callback, const Requester&& requester, std::string_view data) override;
    void Update(CALLBACK_&& callback, const Requester&& requester, std::string_view data, std::optional<uint64_t> _id) override;
    void Delete(CALLBACK_&& callback, const Requester&& requester, std::optional<uint64_t> _id) override;
    void Search(CALLBACK_&& callback, const Requester&& requester, std::string_view data) override;
};
