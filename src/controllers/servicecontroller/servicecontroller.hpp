
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

template <Service_t T>
void ServiceController<T>::Create(CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
    EntityController<T>::Create(std::move(callback), std::move(requester), data);
}
template <Service_t T>
void ServiceController<T>::Read(CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
    EntityController<T>::Read(std::move(callback), std::move(requester), data);
}

template <Service_t T>
void ServiceController<T>::Update(CALLBACK_&& callback, const Requester&& requester, std::string_view data, const std::optional<uint64_t> id)
{
    EntityController<T>::Update(std::move(callback), std::move(requester), data, id);
}

template <Service_t T>
void ServiceController<T>::Delete(CALLBACK_&& callback, const Requester&& requester, const std::optional<uint64_t> id)
{
    EntityController<T>::Delete(std::move(callback), std::move(requester), id);
}

template <Service_t T>
void ServiceController<T>::Search(CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
    EntityController<T>::Search(std::move(callback), std::move(requester), data);
}
