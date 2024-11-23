
#pragma once

#include "controllers/entitycontroller/entitycontroller.hpp"
#include "controllers/servicecontroller/servicecontrollerbase.hpp"

template <typename T, typename CALLBACK>
class ServiceController : public EntityController<T, CALLBACK>, public ServiceControllerBase<CALLBACK>
{
   public:
    explicit ServiceController() = default;

    virtual ~ServiceController() override = default;

    // CRUDS
    void Create(CALLBACK &&callback, std::string_view data) override;
    void Read(CALLBACK &&callback, std::string_view data) override;
    void Update(CALLBACK &&callback, std::string_view data, std::optional<uint64_t> id) override;
    void Delete(CALLBACK &&callback, std::optional<uint64_t> id) override;
    void Search(CALLBACK &&callback, std::string_view data) override;
};

template <typename T, typename CALLBACK>
void ServiceController<T, CALLBACK>::Create(CALLBACK &&callback, std::string_view data)
{
    EntityController<T, CALLBACK>::Create(std::move(callback), data);
}
template <typename T, typename CALLBACK>
void ServiceController<T, CALLBACK>::Read(CALLBACK &&callback, std::string_view data)
{
    EntityController<T, CALLBACK>::Read(std::move(callback), data);
}

template <typename T, typename CALLBACK>
void ServiceController<T, CALLBACK>::Update(CALLBACK &&callback, std::string_view data, const std::optional<uint64_t> id)
{
    EntityController<T, CALLBACK>::Update(std::move(callback), data, id);
}

template <typename T, typename CALLBACK>
void ServiceController<T, CALLBACK>::Delete(CALLBACK &&callback, const std::optional<uint64_t> id)
{
    EntityController<T, CALLBACK>::Delete(std::move(callback), id);
}

template <typename T, typename CALLBACK>
void ServiceController<T, CALLBACK>::Search(CALLBACK &&callback, std::string_view data)
{
    EntityController<T, CALLBACK>::Search(std::move(callback), data);
}
