#pragma once

#include <functional>
#include <string_view>

#include "utils/global/callback.hpp"
class AppointmentControllerBase
{
   public:
    explicit AppointmentControllerBase() = default;

    virtual ~AppointmentControllerBase() = default;

    // CRUDS
    virtual void Create(CALLBACK_ &&callback, std::string_view data)                             = 0;
    virtual void Read(CALLBACK_ &&callback, std::string_view data)                               = 0;
    virtual void Update(CALLBACK_ &&callback, std::string_view data, std::optional<uint64_t> id) = 0;
    virtual void Delete(CALLBACK_ &&callback, std::optional<uint64_t> id)                        = 0;
    virtual void Search(CALLBACK_ &&callback, std::string_view data)                             = 0;
};