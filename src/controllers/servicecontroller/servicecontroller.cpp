

#include "controllers/servicecontroller/servicecontroller.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include "controllers/appointmentcontroller/appointmentcontrollerbase.hpp"
#include "controllers/entitycontroller/entitycontroller.hpp"
#include "entities/services/clinics/clinics.hpp"   // IWYU pragma: keep
#include "entities/services/laboratories.hpp"      // IWYU pragma: keep
#include "entities/services/pharmacies.hpp"        // IWYU pragma: keep
#include "entities/services/radiologycenters.hpp"  // IWYU pragma: keep
#include "utils/global/callback.hpp"
#include "utils/global/concepts.hpp"

template <Service_t T>
void ServiceController<T>::Create(CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
    EntityController<T>::Create(std::move(callback), std::forward<const Requester&&>(requester), data);
}
template <Service_t T>
void ServiceController<T>::Read(CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
    EntityController<T>::Read(std::move(callback), std::forward<const Requester&&>(requester), data);
}

template <Service_t T>
void ServiceController<T>::Update(CALLBACK_&& callback, const Requester&& requester, std::string_view data, const std::optional<uint64_t> _id)
{
    EntityController<T>::Update(std::move(callback), std::forward<const Requester&&>(requester), data, _id);
}

template <Service_t T>
void ServiceController<T>::Delete(CALLBACK_&& callback, const Requester&& requester, const std::optional<uint64_t> _id)
{
    EntityController<T>::Delete(std::move(callback), std::forward<const Requester&&>(requester), _id);
}

template <Service_t T>
void ServiceController<T>::Search(CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
    EntityController<T>::Search(std::move(callback), std::forward<const Requester&&>(requester), data);
}

#define INSTANTIATE_SERVICE_CONTROLLER(TYPE) /*NOLINT*/                                                                                                  \
    template void ServiceController<TYPE>::Create(CALLBACK_&& callback, const Requester&& requester, std::string_view data);                             \
    template void ServiceController<TYPE>::Read(CALLBACK_&& callback, const Requester&& requester, std::string_view data);                               \
    template void ServiceController<TYPE>::Update(CALLBACK_&& callback, const Requester&& requester, std::string_view data, std::optional<uint64_t> id); \
    template void ServiceController<TYPE>::Delete(CALLBACK_&& callback, const Requester&& requester, std::optional<uint64_t> id);                        \
    template void ServiceController<TYPE>::Search(CALLBACK_&& callback, const Requester&& requester, std::string_view data);

// Instantiate for all entity types

INSTANTIATE_SERVICE_CONTROLLER(Clinics)
INSTANTIATE_SERVICE_CONTROLLER(Pharmacies)
INSTANTIATE_SERVICE_CONTROLLER(Laboratories)
INSTANTIATE_SERVICE_CONTROLLER(RadiologyCenters)
