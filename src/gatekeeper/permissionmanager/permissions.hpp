/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file permissions.hpp
 * @brief A small type that stores permissions as on/off bits in one byte.
 *        (Originally: "Holds a bitmask of authorization bits and the helpers to
 * combine/test them.")
 * @details This header defines the Permissions struct. It lists the individual
 *          rights (like "can read" or "is owner") as single bits, and provides
 *          operators to combine and test those bits. A single value can
 * represent several rights at once.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Authorization questions ("may this person delete this?") are answered using
 * these bit masks. PermissionManager and PermissionManagerPrivate call the
 * helpers here to turn a number stored in the database into a clear yes/no.
 * Without this file there would be no common language for describing rights.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Bit masks and bitwise operators:
 * https://en.wikipedia.org/wiki/Mask_(computing)
 * - C++ bitwise operators:
 * https://en.cppreference.com/w/cpp/language/operator_arithmetic
 * - enum class (scoped enumerations):
 * https://en.cppreference.com/w/cpp/language/enum
 * - uint8_t (an exact 8-bit integer):
 * https://en.cppreference.com/w/cpp/types/integer
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per source file.

#include <cstddef>  // std::size_t (used indirectly by the fixed-width types).
// std::uint8_t and std::uint64_t: integers with an exact number of bits.
#include <cstdint>

/**
 * @struct Permissions
 * @brief Holds a bitmask of authorization bits and the helpers to combine/test
 * them.
 * @details Powers are single-bit flags stored in a uint8_t. Two groups of bits
 * are distinguished by convention: identity bits (USER, PROVIDER, IS_ADMIN,
 * IS_OWNER, IS_SUPER) and capability bits (CAN_READ, CAN_WRITE, CAN_DELETE). A
 * value is a bitwise combination, so a single mask can express roles plus
 * capabilities.
 * @note All the test helpers (hasPower(), hasMatch(), operator&) answer "any
 * bit in common", not "all requested bits present".
 *
 * In everyday words: imagine an 8-switch control panel, one switch per right.
 * Each switch is a power of two (1, 2, 4, 8, ...). Turning on several switches
 * and adding the numbers produces a single value that remembers exactly which
 * switches are on. Testing "is any of these switches on?" is a fast bitwise
 * AND.
 */
struct Permissions  // A value type; it simply groups the bit helpers together.
{
 public:
  /**
   * @brief Individual authorization bits. Values are powers of two and may be
   * OR-ed together.
   * @note Only NONE and CAN_READ/CAN_WRITE/CAN_DELETE are consumed in src/;
   * USER, PROVIDER and IS_SUPER are declared but identity is enforced via
   * Requester string helpers.
   */
  enum class PowerLevel : std::uint8_t  // Named on/off switches stored in
                                        // exactly one byte (8 bits).
  {
    NONE       = 0x00U, /**< No powers; 00000000. */
    USER       = 0x01U, /**< Identity bit for ordinary users; 00000001. */
    PROVIDER   = 0x02U, /**< Identity bit for providers; 00000010. */
    CAN_READ   = 0x04U, /**< Capability: may read; 00000100. */
    CAN_WRITE  = 0x08U, /**< Capability: may create/update; 00001000. */
    CAN_DELETE = 0x10U, /**< Capability: may delete; 00010000. */
    IS_ADMIN   = 0x20U, /**< Identity bit for administrators; 00100000. */
    IS_OWNER   = 0x40U, /**< Identity bit for the service owner; 01000000. */
    IS_SUPER   = 0x80U, /**< Identity bit for super-users; 10000000. */
  };  // 10000000 is the largest single bit; every value above is a power of
      // two.

  /** @brief Constructs a rule wrapping a single (possibly combined)
   * PowerLevel mask. */
  // constructor for rule
  explicit Permissions(PowerLevel _powerLevel) : powerLevel(_powerLevel)
  {
  }  // Stores the supplied mask; "explicit"
     // prevents accidental implicit conversions.
  virtual ~Permissions() =
      default;  // Virtual destructor allows safe use through a base pointer.
  Permissions() = delete;  // No default: a rule is meaningless without a mask.
  Permissions(const Permissions&) =
      default;  // Copy is fine; the value is just a byte.
  Permissions& operator=(const Permissions&) =
      default;  // Copy assignment is fine for the same reason.
  Permissions(Permissions&&) =
      default;  // Move is fine; there is nothing to transfer.
  Permissions& operator=(Permissions&&) =
      default;  // Move assignment is fine for the same reason.

  /**
   * @brief Tests whether any bit in @p check is present in this rule's mask.
   * @param[in] check Mask of bits to look for.
   * @return `true` if at least one checked bit is set.
   */
  bool hasPower(const PowerLevel& check)
  {
    return static_cast<int>(powerLevel & check) != 0;
  }  // ANDs the masks; non-zero means at least one requested bit is present.

  /** @brief Returns the bitwise OR of two masks. */
  // Overload | operator
  friend PowerLevel operator|(
      PowerLevel first,
      PowerLevel second)  // Combines two masks so the result holds every bit
                          // from both.
  {
    return static_cast<PowerLevel>(
        static_cast<uint8_t>(first) |
        static_cast<uint8_t>(second));  // Work on raw bytes, then wrap back
                                        // into the enum type.
  }

  /** @brief Returns the bitwise AND of two masks (non-zero means a shared
   * bit). */
  // Overload & operator
  friend PowerLevel operator&(
      PowerLevel first,
      PowerLevel
          second)  // Keeps only the bits present in both masks (the overlap).
  {
    return static_cast<PowerLevel>(
        static_cast<uint8_t>(first) &
        static_cast<uint8_t>(
            second));  // A non-zero result means they share a right.
  }

  /** @brief Adds the bits of @p second to @p first in place. @return
   * Reference to @p first. */
  // Overload |= operator
  friend PowerLevel& operator|=(PowerLevel& first, PowerLevel second)
  {
    first = first | second;  // Add the other mask's bits to this one.
    return first;            // Return by reference so calls can be chained.
  }

  /** @brief Keeps only the bits shared by @p first and @p second. @return
   * Reference to @p first. */
  // Overload &= operator
  friend PowerLevel& operator&=(PowerLevel& first, PowerLevel second)
  {
    first = first & second;  // Keep only the bits the two masks share.
    return first;            // Return by reference so calls can be chained.
  }

  /** @brief Returns the one's-complement of the 8-bit mask. */
  // Invert operator
  friend PowerLevel operator~(PowerLevel value)
  {
    return static_cast<PowerLevel>(~static_cast<uint8_t>(value));
  }  // Flips every bit: on switches become off and vice versa.

  /** @brief Converts to `true` when at least one power bit is set. */
  // Check if any flag is set
  explicit operator bool() const
  {
    return static_cast<int>(powerLevel) != 0;
  }  // True when at least one switch is on; "explicit" avoids accidental use
     // as a number.

  /**
   * @brief A staff member's decoded permission entry for a service.
   * @details `staff_id`/`power` are parsed from an `"<id>:<mask>"` string;
   * `isMember` marks a successful parse.
   */
  using StaffPermission =
      struct staffPermission  // An alias for the small struct below,
                              // describing one staff member's rights.
  {
    uint64_t staff_id = 0; /**< Numeric staff/client id parsed from the
                              entry; uint64_t matches database ids. */
    PowerLevel power = PowerLevel::NONE; /**< Permission mask parsed from the
                                            entry; starts with no rights. */
    bool isMember = false; /**< True when the entry identified the requester;
                              tells a real match from a failed parse. */
  };

 private:  // Callers cannot touch the raw mask directly.
  PowerLevel
      powerLevel; /**< The combined authorization bits, kept private so it can
                     only be changed through the constructor/operators. */
  /** @brief Tests whether @p permission and @p check share any bit. */
  // check if rule is set
  [[nodiscard]] static bool hasMatch(const PowerLevel& permission,
                                     const PowerLevel& check)
  {
    return static_cast<int>(permission & check) != 0;
  }  // Private "any shared bit?" test; [[nodiscard]] warns if the result is
     // ignored.
};
