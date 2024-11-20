
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
    void Create(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data) override;
    void Read(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data) override;
    void Update(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data) override;
    void Delete(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::optional<uint64_t> id) override;
    void Search(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data) override;
};

template <typename T>
void ServiceController<T>::Create(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data)
{
    EntityController<T>::Create(std::move(callback), data);
}
template <typename T>
void ServiceController<T>::Read(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data)
{
    EntityController<T>::Read(std::move(callback), data);
}

template <typename T>
void ServiceController<T>::Update(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data)
{
    EntityController<T>::Update(std::move(callback), data);
}

template <typename T>
void ServiceController<T>::Delete(std::function<void(const drogon::HttpResponsePtr &)> &&callback, const std::optional<uint64_t> id)
{
    EntityController<T>::Delete(std::move(callback), id);
}

template <typename T>
void ServiceController<T>::Search(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data)
{
    EntityController<T>::Search(std::move(callback), data);
}
