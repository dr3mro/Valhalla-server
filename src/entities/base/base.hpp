/**
 * @file base.hpp
 * @brief Default construction; the interface itself carries no state.
 * @details Base is the shared blueprint (an "abstract base class") for every
 * entity in the server. It declares the five SQL statement builders that each
 * concrete entity must supply: create, read, update, delete and search. It also
 * provides a small helper called Binder that safely turns values into numbered
 * placeholders, so data is never pasted into SQL text. The class stores no data
 * of its own; it only fixes the shape that all derived entity classes follow.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the bottom contract of the whole entity layer. Controllers call the
 * five getSql* functions through a Base reference without caring which concrete
 * table is involved, and every concrete entity (Entity, Client, Service,
 * Appointment, Case and friends) inherits from Base. Without it there would be
 * no single way to ask "give me the SQL for this operation", and the safe
 * binding pattern would have to be copied into every table class by hand.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - std::optional: https://en.cppreference.com/w/cpp/utility/optional
 * - Abstract base class / pure virtual function: a function ending in "= 0"
 * forces every child class to implement it, so callers can use one common
 * interface for all of them.
 * - SQL parameter binding: put $1, $2, ... markers in the SQL and pass the real
 * values separately, so user data can never change the meaning of the query;
 * see https://en.cppreference.com/w/cpp or the libpqxx documentation.
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per source file, to avoid
              // duplicate definitions.

// Provides fmt::format, used below to build placeholder strings such as "$1".
#include <fmt/format.h>

// Brings in size_t, the type used for the placeholder counter.
#include <cstddef>
// std::optional lets a builder return "no statement" instead of a fake empty
// string.
#include <optional>
// The PostgreSQL C++ library; pqxx::params holds the values bound to a
// statement.
#include <pqxx/pqxx>
#include <string>  // std::string is the return type of every SQL builder.

/**
 * Base class for all entities in the application.
 * Provides common functionality for creating, reading, updating, deleting, and
 * searching entities. Derived classes must implement the virtual functions to
 * provide the SQL statements for each operation.
 *
 * @details The interface separates deciding what SQL to run from executing it:
 * a caller asks an entity for a statement plus its bound parameters and hands
 * both to the database layer. Every builder returns SQL containing only $1, $2,
 * ... placeholders and appends the actual values to the caller-supplied
 * pqxx::params, so column values are never written into the SQL text. A builder
 * returns std::nullopt when it cannot produce a usable statement.
 * @note The destructor is virtual because entities are used polymorphically
 * through this interface.
 * @see Entity, Binder
 * @note For a beginner: "virtual" means the call is decided at runtime, so a
 * Base pointer can reach the correct derived-class version of each function. "=
 * 0" means this class gives no body and forces children to provide one; that is
 * what makes Base abstract (not instantiable).
 */
class Base {
 public:
  /**
   * @brief Default construction; the interface itself carries no state.
   */
  explicit Base() = default;  // "explicit" with no arguments simply documents
                              // that this is a deliberate default constructor.

  /**
   * @brief Permits safe deletion through a Base pointer.
   */
  virtual ~Base() = default;  // Virtual so deleting a derived object through
                              // a Base* calls the right destructor.

  // Every builder returns SQL with $1, $2, ... placeholders and stores the
  // values in params; values are never written into the SQL text.
  /**
   * @brief Builds the INSERT statement for the entity's payload.
   * @param[out] params Receives the values for the returned placeholders;
   * builders clear it first.
   * @return SQL with $n placeholders, or std::nullopt if the statement cannot
   * be built.
   * @note Values are bound rather than embedded in the returned text.
   */
  virtual std::optional<std::string> getSqlCreateStatement(
      pqxx::params& params) = 0;  // Child supplies the INSERT; the reference
                                  // lets us fill the caller's container.

  /**
   * @brief Builds the SELECT statement for reading one entity by id.
   * @param[out] params Receives the bound id; builders clear the container
   * first.
   * @return SQL with $n placeholders, or std::nullopt if no usable statement
   * can be built (for example, when no id is available).
   */
  virtual std::optional<std::string> getSqlReadStatement(
      pqxx::params& params) = 0;  // Child supplies the single-row SELECT.

  /**
   * @brief Builds the UPDATE statement for the entity's payload.
   * @param[out] params Receives the bound values and id; builders clear the
   * container first.
   * @return SQL with $n placeholders, or std::nullopt if no usable statement
   * can be built.
   */
  virtual std::optional<std::string> getSqlUpdateStatement(
      pqxx::params& params) = 0;  // Child supplies the UPDATE.

  /**
   * @brief Builds the DELETE statement for an entity identified by id.
   * @param[out] params Receives the bound id; builders clear the container
   * first.
   * @return SQL with $n placeholders, or std::nullopt if no usable statement
   * can be built.
   */
  virtual std::optional<std::string> getSqlDeleteStatement(
      pqxx::params& params) = 0;  // Child supplies the DELETE.

  /**
   * @brief Builds the paginated search statement for the entity's table.
   * @param[out] params Receives the bound keyword, limit and offset; builders
   * clear it first.
   * @return SQL with $n placeholders, or std::nullopt if no usable statement
   * can be built.
   * @note Search filters and ordering identifiers cannot be bound and are
   * instead validated before being written into the statement by the concrete
   * value types.
   */
  virtual std::optional<std::string> getSqlSearchStatement(
      pqxx::params& params) = 0;  // Child supplies the paginated SELECT.

 protected:
  // A builder asks the binder how each value is written into the query:
  // bind(value) stores it in params and returns its placeholder ($1, $2,
  // ...).
  /**
   * @brief Appends values to a pqxx::params and hands back the matching SQL
   * placeholders.
   * @details Construction clears the supplied container, so each fresh Binder
   * restarts numbering at $1 for one statement. Routing every value through
   * bind() is what keeps callers from concatenating untrusted data into SQL
   * text.
   * @note Holds a borrowed reference to the params container and must not
   * outlive it.
   * @note For a beginner: this is a helper struct that derived builders
   * create on the stack for one SQL statement. It remembers a running counter
   * so the first value becomes $1, the second $2, and so on, which is exactly
   * what PostgreSQL expects in the prepared query.
   */
  struct Binder {
    /**
     * @brief Binds to p and clears it so the first bind() produces $1.
     */
    explicit Binder(pqxx::params& p) : params(p) {
      params = {};
    }  // Keep a reference to the caller's container and empty it so
       // placeholders start fresh at $1.

    /**
     * @brief Appends a value to the parameter container and returns its
     * placeholder.
     * @tparam T A type accepted by pqxx::params::append.
     * @param[in] value The value to bind; copied into the container by
     * pqxx.
     * @return A placeholder of the form "$n", with n increasing in call
     * order from 1.
     */
    // A template so any value type pqxx
    // understands can be bound the same way.
    template <typename T>
    std::string bind(const T& value) {
      params.append(value);  // Store the real value separately from the
                             // SQL text (safe binding).
      return fmt::format("${}",
                         ++index);  // Bump the counter first, then return $1,
                                    // $2, ... in that order.
    }

   private:
    /**
     * @brief The borrowed parameter container that receives appended
     * values.
     */
    pqxx::params& params;  // A reference, not a copy: we fill the caller's
                           // container directly; it must outlive this Binder.

    /**
     * @brief Count of values bound so far; drives the $n placeholder
     * numbering.
     */
    size_t index = 0;  // Starts at zero so the first ++index yields 1; this
                       // is the $n counter.
  };
};
