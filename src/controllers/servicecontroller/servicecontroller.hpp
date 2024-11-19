
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
    void Create(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data) override;
    void Read(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data) override;
    void Update(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data) override;
    void Delete(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::optional<uint64_t> id) override;
    void Search(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data) override;
};

#include "controllers/servicecontroller/servicecontroller.tpp"
