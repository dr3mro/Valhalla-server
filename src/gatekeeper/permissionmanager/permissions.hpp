#pragma once

#include <cstddef>
#include <cstdint>

struct Permissions
{
   public:
    enum class PowerLevel : std::uint8_t
    {
        NONE       = 0,       // 00000000
        USER       = 1 << 0,  // 00000001
        PROVIDER   = 1 << 1,  // 00000010
        CAN_READ   = 1 << 2,  // 00000100
        CAN_WRITE  = 1 << 3,  // 00001000
        CAN_DELETE = 1 << 4,  // 00010000
        IS_ADMIN   = 1 << 5,  // 00100000
        IS_OWNER   = 1 << 6,  // 01000000
        IS_SUPER   = 1 << 7,  // 10000000
    };  // 10000000

    // constructor for rule
    explicit Permissions(PowerLevel _powerLevel) { powerLevel = _powerLevel; }
    virtual ~Permissions() = default;
    Permissions()          = delete;

    bool hasPower(const PowerLevel &check) { return static_cast<int>(powerLevel & check) != 0; }

    // Overload | operator
    friend PowerLevel operator|(PowerLevel a, PowerLevel b) { return static_cast<PowerLevel>(static_cast<int>(a) | static_cast<int>(b)); }

    // Overload & operator
    friend PowerLevel operator&(PowerLevel a, PowerLevel b) { return static_cast<PowerLevel>(static_cast<int>(a) & static_cast<int>(b)); }

    // Overload |= operator
    friend PowerLevel &operator|=(PowerLevel &a, PowerLevel b)
    {
        a = a | b;
        return a;
    }

    // Overload &= operator
    friend PowerLevel &operator&=(PowerLevel &a, PowerLevel b)
    {
        a = a & b;
        return a;
    }

    // Invert operator
    friend PowerLevel operator~(PowerLevel a) { return static_cast<PowerLevel>(~static_cast<int>(a)); }

    // Check if any flag is set
    explicit operator bool() const { return static_cast<int>(powerLevel) != 0; }
    using StaffPermission = struct staffPermission
    {
        uint64_t   id       = 0;
        PowerLevel power    = PowerLevel::NONE;
        bool       isMember = false;
    };

   private:
    PowerLevel powerLevel;
    // check if rule is set
    bool hasMatch(const PowerLevel &permission, const PowerLevel &check) const { return static_cast<int>(permission & check) != 0; }
};