

#include "controllers/servicecontroller/servicecontroller.hpp"

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

#define INSTANTIATE_SERVICE_CONTROLLER(TYPE)                                                                                                             \
    template void ServiceController<TYPE>::Create(CALLBACK_&& callback, const Requester&& requester, std::string_view data);                             \
    template void ServiceController<TYPE>::Read(CALLBACK_&& callback, const Requester&& requester, std::string_view data);                               \
    template void ServiceController<TYPE>::Update(CALLBACK_&& callback, const Requester&& requester, std::string_view data, std::optional<uint64_t> id); \
    template void ServiceController<TYPE>::Delete(CALLBACK_&& callback, const Requester&& requester, std::optional<uint64_t> id);                        \
    template void ServiceController<TYPE>::Search(CALLBACK_&& callback, const Requester&& requester, std::string_view data);

#include "entities/services/clinics/clinics.hpp"   // IWYU pragma : keep
#include "entities/services/laboratories.hpp"      // IWYU pragma : keep
#include "entities/services/pharmacies.hpp"        // IWYU pragma: keep
#include "entities/services/radiologycenters.hpp"  // IWYU pragma : keep
// Instantiate for all entity types

INSTANTIATE_SERVICE_CONTROLLER(Clinics)
INSTANTIATE_SERVICE_CONTROLLER(Pharmacies)
INSTANTIATE_SERVICE_CONTROLLER(Laboratories)
INSTANTIATE_SERVICE_CONTROLLER(RadiologyCenters)