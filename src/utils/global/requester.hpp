#pragma once
#include <cstdint>
#include <string>
#include <utility>
namespace api::v2
{
    using Requester = struct Requester
    {
       private:
        uint64_t    id;
        std::string group;

       public:
        enum class GroupId : uint8_t
        {
            Users,
            Providers,
            Owners,
            Admin,
            Super,
            Staff
        };

        explicit Requester(uint64_t _id, std::string _group) : id(_id), group(std::move(_group)) {}
        Requester() = delete;

        Requester(const Requester &) = default;
        Requester(Requester &&)      = default;

        Requester &operator=(const Requester &) = default;
        Requester &operator=(Requester &&)      = default;

        virtual ~Requester() = default;

        [[nodiscard]] uint64_t           getId() const { return id; }
        [[nodiscard]] const std::string &getGroup() const { return group; }
        [[nodiscard]] bool               isAdmin() const { return groupMatch("admins"); }
        [[nodiscard]] bool               isUser() const { return groupMatch("users"); }
        [[nodiscard]] bool               isProvider() const { return groupMatch("providers"); }
        [[nodiscard]] bool               isStaff() const { return groupMatch("staff"); }
        [[nodiscard]] bool               isOwner() const { return groupMatch("owners"); }
        [[nodiscard]] bool               isSuper() const { return groupMatch("Super"); }
        [[nodiscard]] bool               groupMatch(const std::string &_group) const { return this->group == _group; }
        [[nodiscard]] bool               idMatch(uint64_t _id) const { return this->id == _id; }
        void                             setId(uint64_t _id) { this->id = _id; }
        void                             setGroup(const std::string &group) { this->group = group; }
    };
}  // namespace api::v2

// [ ] Make group as enum and create a static get description function