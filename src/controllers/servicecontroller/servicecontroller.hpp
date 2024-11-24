
#pragma once

#include "controllers/entitycontroller/entitycontroller.hpp"
#include "controllers/servicecontroller/servicecontrollerbase.hpp"

template <typename T>
class ServiceController : public EntityController<T>, public ServiceControllerBase
{
   public:
    explicit ServiceController() = default;

    virtual ~ServiceController() override = default;

    // CRUDS
    void Create(CALLBACK_ &&callback, std::string_view data) override;
    void Read(CALLBACK_ &&callback, std::string_view data) override;
    void Update(CALLBACK_ &&callback, std::string_view data, std::optional<uint64_t> id) override;
    void Delete(CALLBACK_ &&callback, std::optional<uint64_t> id) override;
    void Search(CALLBACK_ &&callback, std::string_view data) override;
};

template <typename T>
void ServiceController<T>::Create(CALLBACK_ &&callback, std::string_view data)
{
    EntityController<T>::Create(std::move(callback), data);
}
template <typename T>
void ServiceController<T>::Read(CALLBACK_ &&callback, std::string_view data)
{
    EntityController<T>::Read(std::move(callback), data);
}

template <typename T>
void ServiceController<T>::Update(CALLBACK_ &&callback, std::string_view data, const std::optional<uint64_t> id)
{
    EntityController<T>::Update(std::move(callback), data, id);
}

template <typename T>
void ServiceController<T>::Delete(CALLBACK_ &&callback, const std::optional<uint64_t> id)
{
    EntityController<T>::Delete(std::move(callback), id);
}

template <typename T>
void ServiceController<T>::Search(CALLBACK_ &&callback, std::string_view data)
{
    EntityController<T>::Search(std::move(callback), data);
}
