#pragma once

#include <optional>
#include <string_view>

#include "utils/global/callback.hpp"
#include "utils/global/requester.hpp"

using namespace api::v2;
class AppointmentControllerBase
{
   public:
    explicit AppointmentControllerBase() = default;

    virtual ~AppointmentControllerBase() = default;

    // CRUDS
    virtual void Create(CALLBACK_&& callback, const Requester&& requester, std::string_view data)                             = 0;
    virtual void Read(CALLBACK_&& callback, const Requester&& requester, std::string_view data)                               = 0;
    virtual void Update(CALLBACK_&& callback, const Requester&& requester, std::string_view data, std::optional<uint64_t> id) = 0;
    virtual void Delete(CALLBACK_&& callback, const Requester&& requester, std::optional<uint64_t> id)                        = 0;
    virtual void Search(CALLBACK_&& callback, const Requester&& requester, std::string_view data)                             = 0;
};