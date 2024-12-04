
#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

#include "utils/global/callback.hpp"

class ServiceControllerBase
{
   public:
    explicit ServiceControllerBase() = default;

    virtual ~ServiceControllerBase() = default;

    // CRUDS
    virtual void Create(CALLBACK_&& callback, std::string_view data)                             = 0;
    virtual void Read(CALLBACK_&& callback, std::string_view data)                               = 0;
    virtual void Update(CALLBACK_&& callback, std::string_view data, std::optional<uint64_t> id) = 0;
    virtual void Delete(CALLBACK_&& callback, std::optional<uint64_t> id)                        = 0;
    virtual void Search(CALLBACK_&& callback, std::string_view data)                             = 0;
};
