/**
 * @file requester.hpp
 * @brief Caller identity used by the permission layer.
 * @details Defines the Requester value object, which carries two pieces of
 *          information about who made an API request: an opaque numeric id and
 * a group name. It offers simple getters, setters and matching helpers such as
 * isAdmin() or idMatch(). A GroupId enum lists known groups, but the actual
 * checks compare group names as plain strings.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * The gatekeeper and permission manager need to know who is calling before they
 * allow an action. Controllers copy a Requester into the work they dispatch to
 * other threads, so the checks still see the original caller's identity. The
 * isX helpers (isAdmin, isProvider, ...) let permission rules read like plain
 * English. Without this object there would be no consistent way to carry caller
 * identity from the HTTP layer down to the permission checks.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Classes and access specifiers (public/private):
 * https://en.cppreference.com/w/cpp/language/access
 * - special member functions (= default / = delete):
 * https://en.cppreference.com/w/cpp/language/member_functions#Special_member_functions
 * - [[nodiscard]] attribute:
 * https://en.cppreference.com/w/cpp/language/attributes/nodiscard
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per translation unit.
// Provides fixed-width integer types such as uint64_t and uint8_t.
#include <cstdint>
#include <string>  // std::string holds the group name.
// std::move is used in the constructor to avoid copying the group string.
#include <utility>
namespace api::v2  // Groups this type with the version-2 API code.
{
  /**
   * @struct Requester
   * @brief Value object describing who issued a request.
   * @details Holds an opaque numeric id and a group name. It is copied into
   *          lambdas dispatched to worker threads and consulted by the
   *          permission manager, so instances are cheap value types with
   *          defaulted copy/move operations.
   * @note Group checks are literal string comparisons against hardcoded names
   *       such as "admins", "users", "providers", "staff" and "owners"
   *       (isSuper matches "Super" with a capital S). The GroupId enumeration
   *       is declared but is not used to validate or store `group`.
   */
  using Requester = struct Requester  // A named struct exposed through a
                                      // "using" alias so both spellings work.
  {
   private:
    /**
     * @brief Opaque identity of the requester, compared by idMatch.
     */
    uint64_t id;  // The caller's numeric identity; a fixed 64-bit unsigned
                  // type gives a wide, portable range.
    /**
     * @brief Group name, compared literally by groupMatch and the isX
     * helpers.
     */
    std::string group;  // The caller's group as text, e.g. "admins";
                        // compared exactly, so spelling/case matter.

   public:
    /**
     * @enum GroupId
     * @brief Named requester groups.
     * @details Values are Users, Providers, Owners, Admin, Super and Staff.
     *          This enumerator is informational: group membership is stored
     *          and checked as a string, not as a GroupId.
     */
    enum class GroupId : uint8_t  // A scoped enum (accessed as
                                  // GroupId::Users) stored compactly as
                                  // 8-bit integers.
    {
      Users,      // A normal user account.
      Providers,  // A healthcare provider account.
      Owners,     // An owner account.
      Admin,      // An administrator account.
      Super,      // A super-user account (note: the check uses the string
                  // "Super", capital S).
      Staff       // A staff account.
    };

    /**
     * @brief Constructs a requester with a known id and group.
     * @param[in] _id Opaque numeric identity (moved into the id field).
     * @param[in] _group Group name, copied/moved into the group field.
     */
    explicit Requester(uint64_t _id, std::string _group)
        : id(_id), group(std::move(_group))
    {
    }  // "explicit" prevents accidental implicit conversion from two
       // values; std::move avoids copying the string.
    /**
     * @brief Default construction is disabled; an id and group are
     * mandatory.
     */
    Requester() = delete;  // A requester without an identity makes no
                           // sense, so this constructor is removed.

    /**
     * @brief Copy construction; Requester is a cheap value type.
     */
    Requester(const Requester&) =
        default;  // Let the compiler copy the id and string normally.
    /**
     * @brief Move construction; Requester is a cheap value type.
     */
    Requester(Requester&&) =
        default;  // Let the compiler move the string for efficiency.

    /**
     * @brief Copy assignment; Requester is a cheap value type.
     */
    Requester& operator=(const Requester&) =
        default;  // Default copy assignment copies both fields.
    /**
     * @brief Move assignment; Requester is a cheap value type.
     */
    Requester& operator=(Requester&&) =
        default;  // Default move assignment moves the string
                  // instead of copying it.

    /**
     * @brief Virtual destructor; the owned group string is freed normally.
     */
    virtual ~Requester() = default;  // Virtual so derived types can be
                                     // destroyed safely through a base pointer.

    /**
     * @brief Returns the requester's opaque id.
     */
    [[nodiscard]] uint64_t getId() const
    {
      return id;
    }  // [[nodiscard]] warns if the caller ignores the returned id; const
       // means no change to the object.
    /**
     * @brief Returns the requester's group name.
     */
    [[nodiscard]] const std::string& getGroup() const
    {
      return group;
    }  // Returns by const reference to avoid copying the string.
    /**
     * @brief Tests membership of the "admins" group.
     */
    [[nodiscard]] bool isAdmin() const
    {
      return groupMatch("admins");
    }  // Convenience wrapper around the literal string comparison.
    /**
     * @brief Tests membership of the "users" group.
     */
    [[nodiscard]] bool isUser() const
    {
      return groupMatch("users");
    }  // True when the stored group is exactly "users".
    /**
     * @brief Tests membership of the "providers" group.
     */
    [[nodiscard]] bool isProvider() const
    {
      return groupMatch("providers");
    }  // True when the stored group is exactly "providers".
    /**
     * @brief Tests membership of the "staff" group.
     */
    [[nodiscard]] bool isStaff() const
    {
      return groupMatch("staff");
    }  // True when the stored group is exactly "staff".
    /**
     * @brief Tests membership of the "owners" group.
     */
    [[nodiscard]] bool isOwner() const
    {
      return groupMatch("owners");
    }  // True when the stored group is exactly "owners".
    /**
     * @brief Tests membership of the "Super" group (capital S).
     */
    [[nodiscard]] bool isSuper() const
    {
      return groupMatch("Super");
    }  // Case matters: only the exact string "Super" matches.
    /**
     * @brief Compares the group name literally with `_group`.
     * @param[in] _group Exact group name to match.
     * @return True if the names are equal.
     */
    [[nodiscard]] bool groupMatch(const std::string& _group) const
    {
      return this->group == _group;
    }  // Plain string equality; every isX helper funnels through here.
    /**
     * @brief Compares the stored id with `_id`.
     * @param[in] _id Id to match.
     * @return True if the ids are equal.
     */
    [[nodiscard]] bool idMatch(uint64_t _id) const
    {
      return this->id == _id;
    }  // Used to check e.g. whether a requester owns a particular resource.
    /**
     * @brief Replaces the stored id.
     * @param[in] _id New id.
     */
    void setId(uint64_t _id)
    {
      this->id = _id;
    }  // Overwrites the identity with the supplied number.
    /**
     * @brief Replaces the stored group name.
     * @param[in] group New group name; must match the literal used by the
     *          relevant isX helper for permission checks to succeed.
     */
    void setGroup(const std::string& group)
    {
      this->group = group;
    }  // Takes a const reference and copies it into the stored string.
  };
}  // namespace api::v2

// [ ] Make group as enum and create a static get description function
