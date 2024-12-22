#pragma once

#include <cstddef>
#include <cstdint>

struct Permissions
{
   public:
    enum class PowerLevel : std::uint8_t
    {
        NONE       = 0x00U,  // 00000000
        USER       = 0x01U,  // 00000001
        PROVIDER   = 0x02U,  // 00000010
        CAN_READ   = 0x04U,  // 00000100
        CAN_WRITE  = 0x08U,  // 00001000
        CAN_DELETE = 0x10U,  // 00010000
        IS_ADMIN   = 0x20U,  // 00100000
        IS_OWNER   = 0x40U,  // 01000000
        IS_SUPER   = 0x80U,  // 10000000
    };  // 10000000

    // constructor for rule
    explicit Permissions(PowerLevel _powerLevel) : powerLevel(_powerLevel) {}
    virtual ~Permissions()                      = default;
    Permissions()                               = delete;
    Permissions(const Permissions &)            = default;
    Permissions &operator=(const Permissions &) = default;
    Permissions(Permissions &&)                 = default;
    Permissions &operator=(Permissions &&)      = default;

    bool hasPower(const PowerLevel &check) { return static_cast<int>(powerLevel & check) != 0; }

    // Overload | operator
    friend PowerLevel operator|(PowerLevel first, PowerLevel second)
    {
        return static_cast<PowerLevel>(static_cast<uint8_t>(first) | static_cast<uint8_t>(second));
    }

    // Overload & operator
    friend PowerLevel operator&(PowerLevel first, PowerLevel second)
    {
        return static_cast<PowerLevel>(static_cast<uint8_t>(first) & static_cast<uint8_t>(second));
    }

    // Overload |= operator
    friend PowerLevel &operator|=(PowerLevel &first, PowerLevel second)
    {
        first = first | second;
        return first;
    }

    // Overload &= operator
    friend PowerLevel &operator&=(PowerLevel &first, PowerLevel second)
    {
        first = first & second;
        return first;
    }

    // Invert operator
    friend PowerLevel operator~(PowerLevel value) { return static_cast<PowerLevel>(~static_cast<uint8_t>(value)); }

    // Check if any flag is set
    explicit operator bool() const { return static_cast<int>(powerLevel) != 0; }
    using StaffPermission = struct staffPermission
    {
        uint64_t   staff_id = 0;
        PowerLevel power    = PowerLevel::NONE;
        bool       isMember = false;
    };

   private:
    PowerLevel powerLevel;
    // check if rule is set
    [[nodiscard]] static bool hasMatch(const PowerLevel &permission, const PowerLevel &check) { return static_cast<int>(permission & check) != 0; }
};
