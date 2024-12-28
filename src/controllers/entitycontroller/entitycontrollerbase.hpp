#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "utils/global/callback.hpp"
#include "utils/global/requester.hpp"

using Requester = api::v2::Requester;
class EntityControllerBase
{
   public:
    explicit EntityControllerBase()                              = default;
    EntityControllerBase(const EntityControllerBase&)            = default;
    EntityControllerBase(EntityControllerBase&&)                 = default;
    EntityControllerBase& operator=(const EntityControllerBase&) = default;
    EntityControllerBase& operator=(EntityControllerBase&&)      = default;

    virtual ~EntityControllerBase() = default;

    // CRUDS
    virtual void Create(CALLBACK_&& callback, const Requester&& requester, std::string_view data)                              = 0;
    virtual void Read(CALLBACK_&& callback, const Requester&& requester, std::string_view data)                                = 0;
    virtual void Update(CALLBACK_&& callback, const Requester&& requester, std::string_view data, std::optional<uint64_t> _id) = 0;
    virtual void Delete(CALLBACK_&& callback, const Requester&& requester, std::optional<uint64_t> _id)                        = 0;
    virtual void Search(CALLBACK_&& callback, const Requester&& requester, std::string_view data)                              = 0;
};
