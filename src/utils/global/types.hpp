/**
 * @file types.hpp
 * @brief Shared value types and their hash specialisations.
 * @details Defines the ColumnInfo struct, which describes one database column,
 *          and teaches std::hash how to hash it (and a pair of strings).
 * Hashing matters because these types are used as keys in unordered containers
 *          such as unordered_set. Equality and hashing deliberately look at the
 *          same three fields so that two "equal" objects always get the same
 * hash.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * The schema validator compares database table descriptions. It stores columns
 * in hash-based sets, which need both an operator== and a std::hash to work.
 * This file provides both, and deliberately ignores the "Constraint" text so a
 * column is recognised by its name, data type and nullability alone. Without
 * these definitions the unordered sets would not compile for ColumnInfo.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Template specializations:
 * https://en.cppreference.com/w/cpp/language/template_specialization
 * - std::hash: https://en.cppreference.com/w/cpp/utility/hash
 * - operator== :
 * https://en.cppreference.com/w/cpp/language/operators#Equality_operators
 * - std::pair: https://en.cppreference.com/w/cpp/utility/pair
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per translation unit.
// std::string is used for the column name, data type and constraint text.
#include <string>
namespace api  // Outer namespace for all API code.
{
  namespace v2  // Nested namespace for version-2 API code.
  {
    /**
     * @struct ColumnInfo
     * @brief Describes one database column as reported by the schema query.
     * @details Used as a key in unordered sets of table schemas. Equality
     * and hashing deliberately ignore Constraint so that the same column is
     * recognised regardless of its default/constraint expression.
     */
    using ColumnInfo =
        struct ColumnInfo  // A named struct exposed via a "using" alias so
                           // both spellings work.
    {
      /**
       * @brief Column name.
       */
      std::string Name;  // The column's name, e.g. "id"; text because SQL
                         // identifiers are words.
      /**
       * @brief Database data type of the column.
       */
      std::string DataType;  // The SQL type, e.g. "integer" or "character
                             // varying".
      /**
       * @brief Constraint/default expression; not part of equality or
       * hashing.
       */
      std::string Constraint;  // Extra SQL such as a DEFAULT value; ignored
                               // when comparing/hashing on purpose.
      /**
       * @brief Whether the column permits NULL values.
       */
      bool isNullable;  // True if the column may hold NULL; a simple
                        // yes/no flag needs only a bool.

      /**
       * @brief Compares name, data type and nullability only.
       * @param[in] other Column to compare against.
       * @return True when the three keyed fields match; Constraint is
       * ignored.
       */
      bool operator==(const ColumnInfo& other) const
      {
        return Name == other.Name && DataType == other.DataType &&
               isNullable == other.isNullable;
      }  // Only the three "key" fields decide equality; Constraint is
         // left out so minor SQL differences do not create false
         // mismatches.
    };
  }  // namespace v2
}  // namespace api

namespace std  // We are allowed to specialise std::hash for our own types by
               // opening namespace std.
{
  /**
   * @brief Hashes ColumnInfo consistently with its operator==.
   * @details Combines Name, DataType and isNullable; Constraint is excluded
   * so equal columns always receive equal hashes.
   */
  template <>  // An explicit specialisation: replace the generic std::hash
               // specifically for ColumnInfo.
  struct hash<api::v2::ColumnInfo>  // The hash object type for
                                    // ColumnInfo keys.
  {
    /** @brief Hashes the three keyed fields, matching
     * ColumnInfo::operator==. */
    size_t operator()(const api::v2::ColumnInfo& column)
        const  // Called by unordered containers to get a hash for a column.
    {
      // Combine hashes of Name, DataType, and isNullable
      size_t h1 = hash<std::string>()(column.Name);  // Hash the name first.
      size_t h2 =
          hash<std::string>()(column.DataType);  // Hash the data type second.
      size_t h3 =
          hash<bool>()(column.isNullable);  // Hash the nullable flag third.
      return h1 ^ (h2 << 1) ^ (h3 << 2);    // Mix the three hashes with XOR and
                                          // bit-shifts so different fields land
                                          // in different bit positions;
                                          // Constraint is excluded on purpose.
    }
  };

  /**
   * @brief Hashes a pair of strings by combining the hashes of its members.
   */
  template <>  // Another explicit specialisation, this time for a pair of
               // strings.
  struct hash<std::pair<std::string,
                        std::string>>  // Lets a (string, string) pair be used
                                       // as a key in unordered containers.
  {
    /** @brief Hashes a string pair by combining its two members' hashes. */
    size_t operator()(const std::pair<std::string, std::string>& pair)
        const  // Receives the pair and produces one combined hash value.
    {
      // Combine the hashes of the pair's two strings.
      size_t h1 = hash<std::string>()(
          pair.first);  // Hash the first string of the pair.
      size_t h2 = hash<std::string>()(
          pair.second);       // Hash the second string of the pair.
      return h1 ^ (h2 << 1);  // Shift the second hash left so the two
                              // parts do not collide as easily.
    }
  };
}  // namespace std
