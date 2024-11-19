#include <drogon/drogon.h>

#include <functional>

template <typename T>
void AppointmentController<T>::Create(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data)
{
    EntityController<T>::Create(std::move(callback), data);
}

template <typename T>
void AppointmentController<T>::Read(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data)
{
    EntityController<T>::Read(std::move(callback), data);
}

template <typename T>
void AppointmentController<T>::Update(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data)
{
    EntityController<T>::Update(std::move(callback), data);
}

template <typename T>
void AppointmentController<T>::Delete(std::function<void(const drogon::HttpResponsePtr &)> &&callback, const std::optional<uint64_t> id)
{
    EntityController<T>::Delete(std::move(callback), id);
}

template <typename T>
void AppointmentController<T>::Search(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data)
{
    EntityController<T>::Search(std::move(callback), data);
}
