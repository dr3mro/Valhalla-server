/**
 * @file entity.hpp
 * @brief Short names for the CRUD value types and the variant they live in (see
 * api::v2::Types).
 * @details This header defines the Entity class, the workhorse that turns a
 * requested operation (create, read, update, delete or search) into a safe SQL
 * statement. It first introduces short aliases for the value types defined in
 * types.hpp so callers do not have to write the long namespace over and over.
 * Entity stores one of those values plus a table name, and exposes five
 * builders - one per operation - that produce SQL with $1, $2, ...
 * placeholders.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Entity is the concrete implementation behind the Base interface. Request
 * controllers construct an Entity for the operation they need and call
 * getSql*Statement() to obtain SQL that the database layer executes. More
 * specialised entities (Client, Service, Case, Appointment) inherit from this
 * class and override only what they must. Without it, every controller would
 * have to know how to spell CREATE/READ/UPDATE/DELETE/SEARCH SQL for every
 * table.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - std::variant: https://en.cppreference.com/w/cpp/utility/variant
 * - std::optional: https://en.cppreference.com/w/cpp/utility/optional
 * - SQL parameter binding: put $1, $2, ... markers in the SQL and pass the real
 * values separately; see https://en.cppreference.com/w/cpp or the libpqxx
 * documentation.
 * - A "type alias" (using X = Y) gives a long type a short local name; no new
 * type is created.
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per source file.

#include <fmt/core.h>  // Core formatting helpers used to assemble SQL strings.
// System type definitions needed by some of the included headers.
#include <sys/types.h>

#include <cstdint>    // Fixed-size integers such as uint64_t, used for ids.
#include <exception>  // std::exception, caught by the builders' error handlers.
// Iterator helpers used by range-based work in the builders.
#include <iterator>
#include <memory>  // std::shared_ptr, used for the cached DatabaseController.
// std::optional, the builders' return type and the id type.
#include <optional>
#include <string>  // std::string for SQL text and the table name.
// std::vector collects column names and values before formatting.
#include <vector>

// DatabaseController: used to ask whether an id already exists.
#include "controllers/databasecontroller/databasecontroller.hpp"
// Base and its Binder helper, which this class inherits and uses.
#include "entities/base/base.hpp"
// The CRUD payload types (Create_t, Read_t, ...) and EntityType variant.
#include "entities/base/types.hpp"
// fmt::format / fmt::join, used to build statements and lists.
#include "fmt/format.h"
// jsoncons::json, the payload type for create/update.
#include "jsoncons/basic_json.hpp"
// Store::cachedObject, used to fetch the shared controller.
#include "store/store.hpp"
// Message::ErrorMessage / CriticalMessage for logging failures.
#include "message/message.hpp"

/**
 * @brief Short names for the CRUD value types and the variant they live in (see
 * api::v2::Types).
 */
using EntityType = api::v2::Types::EntityType;  // One variant that can hold any
                                                // of the operation payloads.
using Create_t =
    api::v2::Types::Create_t;  // Short name for the "insert this JSON" payload.
using Read_t =
    api::v2::Types::Read_t;  // Short name for the "read this id" payload.
using Update_t = api::v2::Types::Update_t;  // Short name for the "change these
                                            // fields" payload.
using Delete_t =
    api::v2::Types::Delete_t;  // Short name for the "delete this id" payload.
using Search_t = api::v2::Types::Search_t;  // Short name for the parsed search
                                            // parameters payload.

/**
 * @class Entity
 * @brief Concrete Base implementation that turns a CRUD value type into SQL
 * statements.
 * @details An Entity pairs one api::v2::Types::EntityType alternative with the
 * database table (also used as the permission "group") it belongs to. The five
 * Base builders are thin adapters that create a fresh Binder and delegate to
 * the build* helpers below; each helper owns the details of one statement
 * shape. Instances are created for each request and are immutable after
 * construction (both the variant and the table name are const), except for the
 * id cached by check_id_exists(). All parameter values go through Binder::bind,
 * so they never appear in the SQL text.
 * @note Neither copy nor move is allowed; construct the entity in place for the
 * request that uses it.
 * @see Base, api::v2::Types::EntityType
 * @note For a beginner: the five getSql*Statement functions are the public
 * "adapters"; each makes a Binder and calls one private-ish build* helper.
 * Splitting them this way keeps the public API small while letting each SQL
 * shape be written and read on its own.
 */
class Entity : public Base {
 public:
  /**
   * @brief Copy and move operations are disabled; entities are request-scoped
   * and constructed in place.
   */
  Entity(const Entity&) =
      delete;  // No copy: an entity belongs to exactly one request.
  Entity(Entity&&) =
      delete;  // No move: keep the object where it was constructed.
  Entity& operator=(const Entity&) =
      delete;  // No copy assignment, for the same reason.
  Entity& operator=(Entity&&) =
      delete;  // No move assignment, for the same reason.

  template <typename T>

  /**
   * @brief Stores _data in the EntityType variant and remembers the target
   * table name.
   * @tparam T One of the alternatives api::v2::Types::EntityType can hold.
   * @param[in] _data The operation-specific payload (Create_t, Read_t, ...).
   * @param[in] _tablename Database table name, also reported as the
   * permission group name.
   * @pre T must be an alternative of EntityType; otherwise the variant
   * construction fails.
   */
  Entity(const T&           _data,
         const std::string& _tablename)
      // Copy the table name, then wrap the payload
      // in the variant so the active alternative
      // records the operation.
      : tablename(_tablename),
        data(EntityType(_data)) {
  }

  /**
   * @brief Destroys the entity; the borrowed DatabaseController is not owned
   * here.
   */
  ~Entity() override = default;  // "override" confirms this matches Base's
                                 // virtual destructor; no cleanup needed.

  /**
   * @brief Builds the INSERT statement via buildCreateStatement().
   * @param[out] params Cleared and filled with the bound column values.
   * @return The INSERT text, or std::nullopt on failure.
   */
  // Override of Base's pure virtual INSERT builder.
  std::optional<std::string> getSqlCreateStatement(
      pqxx::params& params) override {
    Binder binder(params);  // Fresh binder clears params and restarts
                            // numbering at $1.
    return buildCreateStatement(
        binder);  // Delegate the actual SQL shape to the dedicated helper.
  }

  /**
   * @brief Builds the single-row SELECT via buildReadStatement().
   * @param[out] params Cleared and filled with the bound id.
   * @return The SELECT text, or std::nullopt if no id was supplied.
   */
  // Override of Base's single-row SELECT builder.
  std::optional<std::string> getSqlReadStatement(
      pqxx::params& params) override {
    Binder binder(params);  // Fresh binder for this one statement.
    return buildReadStatement(
        binder);  // The helper binds the id and chooses the columns.
  }

  /**
   * @brief Builds the UPDATE statement via buildUpdateStatement().
   * @param[out] params Cleared and filled with the bound SET values and id.
   * @return The UPDATE text, or std::nullopt if no id was supplied.
   */
  // Override of Base's UPDATE builder.
  std::optional<std::string> getSqlUpdateStatement(
      pqxx::params& params) override {
    Binder binder(params);  // Fresh binder so SET values start at $1.
    return buildUpdateStatement(binder);  // Helper turns each JSON field
                                          // into a "column = $n" clause.
  }

  /**
   * @brief Builds the DELETE statement via buildDeleteStatement().
   * @param[out] params Cleared and filled with the bound id.
   * @return The DELETE text, or std::nullopt if no id was supplied.
   */
  // Override of Base's DELETE builder.
  std::optional<std::string> getSqlDeleteStatement(
      pqxx::params& params) override {
    Binder binder(params);  // Fresh binder for the id.
    return buildDeleteStatement(
        binder);  // Helper refuses to build if the id is missing.
  }

  /**
   * @brief Builds the paginated search statement via buildSearchStatement().
   * @param[out] params Cleared and filled with the bound keyword, limit and
   * offset.
   * @return The SELECT text, or std::nullopt on failure.
   */
  // Override of Base's search builder.
  std::optional<std::string> getSqlSearchStatement(
      pqxx::params& params) override {
    Binder binder(params);  // Fresh binder for keyword, limit and offset.
    return buildSearchStatement(binder);  // Helper assembles the ILIKE query
                                          // with validated identifiers.
  }

  /**
   * @brief Exposes the operation-specific payload held in the variant.
   * @return Const reference to the EntityType variant; valid for the entity's
   * lifetime.
   */
  [[nodiscard(
      "Warning: You should never discard the returned "
      "object")]] const EntityType&
  getData() const {
    return data;
  }  // "nodiscard" warns if the caller ignores the value; a const reference
     // avoids copying the variant.

  /**
   * @brief Returns the table name, which doubles as the entity's permission
   * group name.
   * @return The table name supplied at construction.
   */
  [[nodiscard(
      "Warning: You should never discard the returned object")]] std::string
  // ie. tablename; the old name survives as a trailing note.
  getGroupName() const {
    return tablename;  // Return by value; a small string copy is simpler
                       // than exposing internal storage.
  }

  /**
   * @brief Asks the database whether the id carried by the payload already
   * exists in this table.
   * @tparam T The variant alternative whose get_id() should be consulted.
   * @return false when the payload has no id or the database reports no
   * matching row; otherwise the database result. Database failures collapse
   * to false via value_or(false).
   * @note Logs an error message when no id is present. Uses the shared
   * DatabaseController.
   * @throws std::bad_variant_access if T is not the alternative currently
   * held by the variant.
   */
  // Templated so each payload type can be asked for
  // its id uniformly.
  template <typename T>
  bool check_id_exists() {
    std::optional<uint64_t> _id =
        std::get<T>(getData())
            .get_id();  // Pull the id out of the chosen variant
                        // alternative; it may be absent.
    // No id means we cannot check anything useful.
    if (!_id.has_value()) {
      Message::ErrorMessage(
          fmt::format("Failed to create Sql search statement for table {}.",
                      tablename));  // Log why the check is abandoned.
      return false;  // Report "does not exist" so callers can take the
                     // normal failure path.
    }
    auto result =
        databaseController()
            ->checkItemExists(tablename,
                              "id",
                              std::to_string(
                                  _id.value()));  // Ask the shared controller
                                                  // if this id is present;
                                                  // convert the id to text for
                                                  // the helper.
    return result.value_or(false);  // value_or turns a missing/failed result
                                    // into false rather than throwing.
  }

 protected:
  // The SQL statements: one builder per operation. Binder::bind(value)
  // returns the placeholder for that value.
  /**
   * @brief Builds the INSERT statement from a Create_t payload.
   * @details Iterates the JSON object range, binding every value and using
   * the (sorted) keys as column names. An explicit id from the caller is
   * inserted; otherwise "id" is erased so the database assigns it, which also
   * prevents a client from choosing its own primary key.
   * @tparam Binder Binder-like type providing bind().
   * @param[in,out] binder Appends values and returns their placeholders.
   * @return INSERT ... RETURNING *, or std::nullopt if an exception occurs.
   */
  // The helper is a template so it works with Binder or
  // a test double.
  template <typename Binder>
  std::optional<std::string> buildCreateStatement(Binder& binder) {
    // Everything below can throw (JSON access, formatting); one catch
    // keeps callers safe.
    try {
      std::vector<std::string>
          keys_arr;  // Column names collected from the JSON object.
      std::vector<std::string> values_arr;  // Matching placeholders ($1,
                                            // $2, ...) in the same order.

      jsoncons::json data_json =
          std::get<Create_t>(data)
              .get_data();  // Take a copy of the payload so we can edit
                            // it without changing the entity.
      // With an id given (e.g. a patient's health record shares the
      // patient id) it is inserted. Without one the column is left out,
      // so the database assigns it, and any "id" the client sent is
      // dropped.
      if (const auto create_id = std::get<Create_t>(data).get_id();
          // If the caller supplied an explicit
          // primary key...
          create_id.has_value()) {
        data_json["id"] = create_id.value();  // Keep it so the INSERT
                                              // writes that exact id.
      } else {
        data_json.erase("id");  // Otherwise remove any client-sent id
                                // so the database generates one (a
                                // client must not pick its own key).
      }

      for (auto& iterator :
           // Walk each JSON member; object_range
           // may not preserve order, so keys and
           // values are collected in lockstep.
           data_json.object_range()) {
        keys_arr.push_back(
            iterator.key());  // The JSON key becomes a column name.
        values_arr.push_back(binder.bind(
            iterator.value()
                .template as<std::string>()));  // Bind the value as
                                                // text and store the
                                                // returned placeholder.
      }

      // RETURNING * gives all new columns
      // back to the caller.
      // Catch any error from JSON access or formatting.
      return fmt::format("INSERT INTO {} ({}) VALUES ({}) RETURNING *;",
                         tablename,
                         fmt::join(keys_arr, ","),
                         fmt::join(values_arr, ","));
    } catch (const std::exception& e) {
      Message::ErrorMessage(
          fmt::format("Failed to create Sql create statement for table {}.",
                      tablename));  // Log which table failed.
      Message::CriticalMessage(
          e.what());        // Log the low-level reason for debugging.
      return std::nullopt;  // Signal failure instead of returning
                            // half-built SQL.
    }
  }

  /**
   * @brief Builds a single-row SELECT against the table's _safe view.
   * @details An optional schema set limits the selected columns; an empty set
   * selects *. The id is bound. A missing id is a caller error: an error is
   * logged and no statement is returned.
   * @tparam Binder Binder-like type providing bind().
   * @param[in,out] binder Appends the id and returns its placeholder.
   * @return SELECT ... LIMIT 1, or std::nullopt when no id is available or an
   * exception occurs.
   */
  // Builds the "read one row" SELECT.
  template <typename Binder>
  std::optional<std::string> buildReadStatement(Binder& binder) {
    try {
      auto user_id = std::get<Read_t>(data)
                         .get_id();  // The id to look up (may be absent).
      auto schema =
          std::get<Read_t>(data).get_data();  // The set of column names
                                              // the caller wants back.

      std::string columns =
          schema.empty()
              ? "*"
              : fmt::format("{}",
                            fmt::join(schema,
                                      ", "));  // No requested columns means
                                               // "all columns"; otherwise
                                               // join the names with commas.

      // Reading without an id would return the
      // wrong row, so refuse.
      if (!user_id.has_value()) {
        Message::ErrorMessage(
            fmt::format("Failed to read data from table {}. No id provided.",
                        tablename));  // Log the caller mistake.
        return std::nullopt;          // No statement: the caller turns this
                                      // into an error response.
      }
      return fmt::format("SELECT {} FROM {}_safe WHERE id={} LIMIT 1;",
                         columns,
                         tablename,
                         binder.bind(
                             user_id.value()));  // Read from the "_safe" view
                                                 // (a filtered view), bind the
                                                 // id, and LIMIT 1 because ids
                                                 // are unique.
    } catch (const std::exception& e) {
      Message::ErrorMessage(
          fmt::format("Failed to create Sql read statement for table {}.",
                      tablename));         // Log the failing table.
      Message::CriticalMessage(e.what());  // Log the exception details.
      return std::nullopt;  // Report failure without throwing at the
                            // caller.
    }
  }

  /**
   * @brief Builds the UPDATE statement from an Update_t payload.
   * @details Each JSON member becomes one bound "column = $n" SET clause and
   * the id is bound as the final parameter. A missing id aborts the build
   * rather than producing an unscoped UPDATE.
   * @tparam Binder Binder-like type providing bind().
   * @param[in,out] binder Appends SET values and the id, returning their
   * placeholders.
   * @return UPDATE ... RETURNING *, or std::nullopt when no id is available
   * or an exception occurs.
   */
  // Builds the UPDATE ... SET ... WHERE id=... statement.
  template <typename Binder>
  std::optional<std::string> buildUpdateStatement(Binder& binder) {
    try {
      jsoncons::json payload =
          std::get<Update_t>(data)
              .get_data();  // The fields to change, as JSON.

      auto _id =
          std::get<Update_t>(data).get_id();  // The row id from the payload.

      // An UPDATE without an id would change every
      // row - a serious bug - so stop here.
      if (!_id.has_value()) {
        Message::ErrorMessage(
            fmt::format("Failed to update data from table {}. No id provided.",
                        tablename));  // Log the missing id.
        return std::nullopt;          // No statement is safer than an unscoped
                                      // UPDATE.
      }

      std::vector<std::string>
          set_clauses;  // One "column = $n" string per field to change.

      for (auto it = payload.object_range().begin();
           it != payload.object_range().end();
           // Walk every JSON member to build its SET clause.
           ++it) {
        set_clauses.push_back(
            fmt::format("{} = {}",
                        it->key(),
                        binder.bind(
                            it->value()
                                .template as<
                                    std::string>())));  // Column name is text
                                                        // from the validated
                                                        // payload; the value is
                                                        // bound safely.
      }

      return fmt::format("UPDATE {} SET {} WHERE id={} RETURNING *;",
                         tablename,
                         fmt::join(set_clauses, ", "),
                         binder.bind(_id.value()));  // Bind the id LAST so its
                                                     // placeholder number
                                                     // follows the SET values.
    } catch (const std::exception& e) {
      Message::ErrorMessage(
          fmt::format("Failed to create Sql update statement for table {}.",
                      tablename));         // Log the failing table.
      Message::CriticalMessage(e.what());  // Log details.
      return std::nullopt;                 // Signal failure.
    }
  }

  /**
   * @brief Builds the DELETE statement for the id carried by a Delete_t
   * payload.
   * @details The id is bound; a missing id aborts the build so no unscoped
   * DELETE is emitted.
   * @tparam Binder Binder-like type providing bind().
   * @param[in,out] binder Appends the id and returns its placeholder.
   * @return DELETE ... RETURNING id, or std::nullopt when no id is available
   * or an exception occurs.
   */
  // Builds the DELETE for one id.
  template <typename Binder>
  std::optional<std::string> buildDeleteStatement(Binder& binder) {
    try {
      std::optional<uint64_t> _id =
          std::get<Delete_t>(data).get_id();  // The id to delete.
      // Without an id, a DELETE could wipe the
      // table, so refuse.
      if (!_id.has_value()) {
        Message::ErrorMessage(
            fmt::format("Failed to delete data from table {}. No id provided.",
                        tablename));  // Log the mistake.
        return std::nullopt;          // No statement returned.
      }
      return fmt::format("DELETE FROM {} WHERE id={} RETURNING id;",
                         tablename,
                         binder.bind(_id.value()));  // RETURNING id confirms
                                                     // which row was removed.
    } catch (const std::exception& e) {
      Message::ErrorMessage(
          fmt::format("Failed to create Sql delete statement for table {}.",
                      tablename));         // Log the failing table.
      Message::CriticalMessage(e.what());  // Log details.
      return std::nullopt;                 // Signal failure.
    }
  }

  /**
   * @brief Builds the paginated ILIKE search statement for the table's _safe
   * view.
   * @details The keyword is wrapped in %...% for substring matching and bound
   * together with a limit of (requested limit + 1) and the offset; the extra
   * row lets callers detect a further page. filter, order_by and direction
   * are SQL identifiers and are written into the text, so they must already
   * have been validated by Search_t.
   * @tparam Binder Binder-like type providing bind().
   * @param[in,out] binder Appends keyword, limit and offset and returns their
   * placeholders.
   * @return The SELECT text, or std::nullopt if an exception occurs.
   * @note Bindings are made into named locals first because argument
   * evaluation order is unspecified; the placeholder numbering must follow a
   * fixed order.
   */
  // Builds the paginated, case-insensitive search SELECT.
  template <typename Binder>
  std::optional<std::string> buildSearchStatement(Binder& binder) {
    try {
      Search_t searchdata = std::get<Search_t>(
          getData());  // The parsed and validated search parameters.
      // Bound in a fixed order (argument evaluation order is
      // unspecified).
      const std::string keyword_ph = binder.bind(
          fmt::format("%{}%",
                      searchdata.keyword));  // Wrap the keyword in % so ILIKE
                                             // matches anywhere; bind it as $1.
      const std::string limit_ph =
          binder.bind(searchdata.limit + 1);  // Ask for one extra row to detect
                                              // another page; bound as $2.
      const std::string offset_ph = binder.bind(
          searchdata.offset);  // Number of rows to skip; bound as $3.
      return fmt::format(
          "SELECT * FROM {}_safe WHERE {}::text ILIKE {} ORDER BY {} {} "
          "LIMIT {} OFFSET {};",
          tablename,
          searchdata.filter,
          keyword_ph,
          searchdata.order_by,
          searchdata.direction,
          limit_ph,
          offset_ph);  // filter/order_by/direction are pre-validated
                       // identifiers written into the text; values use $n
                       // placeholders.
    } catch (const std::exception& e) {
      Message::ErrorMessage(
          fmt::format("Failed to create Sql search statement for table {}.",
                      tablename));         // Log the failing table.
      Message::CriticalMessage(e.what());  // Log details.
      return std::nullopt;                 // Signal failure.
    }
  }

  /**
   * @brief Table this entity targets; also the group name used by permission
   * checks.
   */
  const std::string tablename;
  /*NOLINT*/  // The table name is fixed at construction, which is why it is
              // const; the lint marker suppresses a style warning about
              // member naming.
  // The database controller is one global object: entities are built for
  // every request, so they do not each carry a pointer to it.
  /**
   * @brief Returns the process-wide DatabaseController from the global object
   * store.
   * @return Shared pointer to the cached controller; entities do not own a
   * per-instance handle.
   */
  static std::shared_ptr<DatabaseController> databaseController() {
    return Store::cachedObject<DatabaseController>();
  }  // Static = one shared accessor for all entities; the Store owns the
     // single controller instance.

 private:
  /**
   * @brief The operation-specific payload; its active alternative selects
   * which builder is valid.
   */
  const EntityType
      data;  // const so the operation cannot change after construction;
             // std::variant records which payload type is active.
};

// [ ] add UUID for all entities and make it autogenerated by PostgreSQL.
// [ ] some optional fields should be checked before get value from them.
