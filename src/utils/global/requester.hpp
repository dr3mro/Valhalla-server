#pragma once
#include <cstdint>
#include <string>
namespace api
{
    namespace v2
    {
        using Requester = struct Requester
        {
            uint64_t    id;
            std::string group;

            bool isUser() const { return group == "Users"; }
            bool isProvider() const { return group == "Providers"; }
        };
    }  // namespace v2
}  // namespace api