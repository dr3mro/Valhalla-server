/**
 * @file types.hpp
 * @brief CRUD operation payload types and the EntityType variant used by entity
 * builders.
 * @details This header declares the small value objects that describe every
 * operation the API can request: Create_t, Read_t, Update_t, Delete_t, plus
 * Search_t, client payloads, suspend and staff data. Most of them derive from a
 * shared Entity_t template that stores a payload and an optional id. At the
 * bottom, EntityType lists them all as one std::variant, which is how an Entity
 * keeps track of which operation it represents. Several types also validate and
 * sanitise input before the SQL builders ever see it.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * These are the data carriers between controllers and the entity layer. A
 * controller parses an HTTP body into one of these types, and the
 * Entity/Client/Service builders read it back to produce SQL. Validation lives
 * here (for example Search_t rejects unsafe column names), so bad input is
 * stopped in one place instead of being trusted by every SQL builder. Without
 * this file the builders would have no typed, validated description of what the
 * client asked for.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - std::variant: https://en.cppreference.com/w/cpp/utility/variant
 * - std::optional: https://en.cppreference.com/w/cpp/utility/optional
 * - SQL parameter binding: put $1, $2, ... markers in the SQL and pass the real
 * values separately; see https://en.cppreference.com/w/cpp or the libpqxx
 * documentation.
 * - A "template" is a blueprint for a class that works with many types. Here
 * the blueprint takes a type parameter named S, and the compiler generates a
 * separate class for each S that is used.
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once             // Include this header at most once per source file.
#include <fmt/core.h>    // Core formatting helpers used in error messages.
#include <fmt/format.h>  // fmt::format, used to build diagnostic strings.

// std::ranges::find_if / all_of, used for scanning and validation.
#include <algorithm>
#include <cctype>  // std::isdigit / std::isalnum, used to validate identifiers.
// Base64 encoding for the invitation link token.
#include <cppcodec/base64_rfc4648.hpp>
#include <cstddef>    // size_t, used for limits and lengths.
#include <cstdint>    // uint64_t, the type used for ids.
#include <exception>  // std::exception, caught around parsing and validation.
// jsoncons::json, the JSON value type used throughout.
#include <jsoncons/basic_json.hpp>
#include <jsoncons/json.hpp>  // JSON parsing helpers (json::parse).
// std::shared_ptr for cached configuration and password hasher.
#include <memory>
#include <optional>   // std::optional for ids and optional parsed values.
#include <stdexcept>  // Standard exception types used by validators.
#include <string>     // std::string for field values.
// std::string_view, a non-owning view of the raw request body.
#include <string_view>
// std::unordered_set for column-name sets and (column, value) pairs.
#include <unordered_set>
#include <utility>  // std::pair, used as the element of data_set.
// std::variant, the EntityType type at the bottom of this file.
#include <variant>

// Configurator: reads server and frontend settings for invitation links.
#include "configurator/configurator.hpp"
// PasswordCrypt: hashes passwords on create.
#include "gatekeeper/passwordcrypt/passwordcrypt.hpp"
// Store::cachedObject, used to fetch shared helpers.
#include "store/store.hpp"
// Http::Error / Status, used to report client vs server errors.
#include "utils/global/http.hpp"
// Message::ErrorMessage / CriticalMessage for logging failures.
#include "message/message.hpp"
// Validator: schema and regex validation of request bodies.
#include "validator/validator.hpp"
// Nested namespace; names here are referenced as
// api::v2::Types::X.
namespace api::v2::Types {
  /**
   * @class Entity_t
   * @brief Immutable payload-plus-optional-id base for the CRUD operation
   * value types.
   * @details Stores the operation's data (a JSON object for create/update, a
   * column set for read, a sentinel for delete/data) together with an
   * optional primary key. The payload and id are const, so an instance is a
   * fixed description of one requested operation. Concrete types below are
   * empty specialisations that choose S and expose construction appropriate
   * to the operation.
   * @tparam S Type of the payload returned by get_data().
   * @see Create_t, Read_t, Update_t, Delete_t, Data_t
   */
  template <typename S>
  struct Entity_t {
   public:
    // Entity_t(const Entity_t &)            = delete;
    // Entity_t(Entity_t &&)                 = delete;
    // Entity_t &operator=(const Entity_t &) = delete;
    // Entity_t &operator=(Entity_t &&)      = delete;

    /**
     * @brief Stores a payload and an optional id.
     * @param[in] _data Operation payload.
     * @param[in] _id Primary key, or std::nullopt when the database should
     * assign/omit it.
     */
    Entity_t(const S&                      _data,
             const std::optional<uint64_t> _id)
        : data(_data),
          id(_id) {
    }  // Copy both inputs into const members so the operation description
       // cannot change later.

    /**
     * @brief Returns the optional primary key associated with the
     * operation.
     */
    [[nodiscard]] std::optional<uint64_t> get_id() const {
      return id;
    }  // Return by value; optional is tiny, and callers can check
       // has_value().

    /**
     * @brief Returns the immutable operation payload.
     */
    [[nodiscard]] const S& get_data() const {
      return this->data;
    }  // Return a const reference to avoid copying potentially large JSON.

    /**
     * @brief Virtual destructor so derived operation types can be destroyed
     * polymorphically.
     */
    virtual ~Entity_t() =
        default;  // Virtual because concrete types inherit and may be
                  // deleted through an Entity_t pointer.

   private:
    /**
     * @brief The immutable operation payload.
     */
    const S data;  // The actual data (JSON, column set or sentinel); const
                   // keeps the request fixed.

    /**
     * @brief The immutable optional primary key.
     */
    const std::optional<uint64_t>
        id;  // An id that may be absent; optional avoids using a fake value
             // like 0 to mean "no id".
  };

  /**
   * @class Create_t
   * @brief Entity_t carrying the JSON object to insert and an optional
   * primary key.
   * @details With an explicit id the insert keeps that column; without one
   * the id column is omitted so the database generates it.
   */
  using Create_t = struct Create_t
      // "using X = struct X ..." gives the
      // anonymous struct the name
      // Create_t.
      : public Entity_t<jsoncons::json> {
    // Create_t(const Create_t &)            = default;
    // Create_t(Create_t &&)                 = default;
    // Create_t &operator=(const Create_t &) = default;
    // Create_t &operator=(Create_t &&)      = default;

    /**
     * @brief Constructs a create operation with a caller-specified id.
     * @param[in] _data JSON object whose members become INSERT columns.
     * @param[in] id Primary key to insert.
     */
    Create_t(const jsoncons::json& _data,
             const uint64_t        id)
        : Entity_t(_data,
                   id) {
    }  // Two-argument form: keep the caller's id in the INSERT.

    // No id: the database assigns it (the column default is nextval of the
    // table's sequence).
    /**
     * @brief Constructs a create operation without an id; the database
     * assigns the primary key.
     * @param[in] _data JSON object whose members become INSERT columns.
     */
    explicit Create_t(const jsoncons::json& _data)
        : Entity_t(_data,
                   std::nullopt) {
    }  // One-argument form: nullopt tells the builder to leave the id
       // column out.

    /**
     * @brief Destroys the create operation.
     */
    ~Create_t() override = default;  // "override" confirms it matches the
                                     // virtual base destructor.
  };

  /**
   * @class Read_t
   * @brief Entity_t carrying the columns to select and the id of the row to
   * read.
   * @details An empty column set means "all columns". The id is mandatory at
   * build time.
   */
  using Read_t =
      struct Read_t : public Entity_t<const std::unordered_set<std::string>> {
    // Read_t(const Read_t &)            = delete;
    // Read_t(Read_t &&)                 = delete;
    // Read_t &operator=(const Read_t &) = delete;
    // Read_t &operator=(Read_t &&)      = delete;

    /**
     * @brief Constructs a read operation for the given id and column set.
     * @param[in] _data Names of the columns to select (empty means all).
     * @param[in] _id Primary key of the row to read.
     */
    Read_t(const std::unordered_set<std::string>& _data,
           const uint64_t                         _id)
        : Entity_t(_data,
                   _id) {
    }  // An unordered set avoids duplicate column names and makes
       // membership checks fast.

    /**
     * @brief Destroys the read operation.
     */
    ~Read_t() override = default;  // No resources to release beyond the base.
  };

  /**
   * @class Update_t
   * @brief Entity_t carrying the JSON object of fields to change and the row
   * id.
   */
  using Update_t = struct Update_t : public Entity_t<jsoncons::json> {
    // Update_t(const Update_t &)            = delete;
    // Update_t(Update_t &&)                 = delete;
    // Update_t &operator=(const Update_t &) = delete;
    // Update_t &operator=(Update_t &&)      = delete;

    /**
     * @brief Constructs an update operation for the given id.
     * @param[in] _data JSON object whose members become SET clauses.
     * @param[in] _id Primary key of the row to update.
     */
    Update_t(const jsoncons::json& _data,
             const uint64_t        _id)
        : Entity_t(_data,
                   _id) {
    }  // The id scopes the UPDATE so it touches only one row.

    /**
     * @brief Destroys the update operation.
     */
    ~Update_t() override = default;  // No resources to release beyond the base.
  };

  /**
   * @class Delete_t
   * @brief Entity_t carrying only the id of the row to delete.
   * @details The char payload is an unused sentinel; the id is the only
   * meaningful state.
   */
  using Delete_t = struct Delete_t : public Entity_t<char> {
    // Delete_t(const Delete_t &)            = delete;
    // Delete_t(Delete_t &&)                 = delete;
    // Delete_t &operator=(const Delete_t &) = delete;
    // Delete_t &operator=(Delete_t &&)      = delete;

    /**
     * @brief Constructs a delete operation for the given id.
     * @param[in] _id Primary key of the row to delete.
     */
    explicit Delete_t(const uint64_t _id)
        : Entity_t('\0',
                   _id) {
    }  // '\0' is a throwaway placeholder because
       // Entity_t still requires some payload type.

    /**
     * @brief Destroys the delete operation.
     */
    ~Delete_t() override = default;  // No resources to release beyond the base.
  };

  /**
   * @class Data_t
   * @brief Entity_t carrying only a client id, used for id-only lookups (for
   * example get-services).
   * @details Like Delete_t, the char payload is an unused sentinel and the id
   * is the state.
   */
  using Data_t = struct Data_t : public Entity_t<char> {
    // Data_t(const Data_t &)            = delete;
    // Data_t(Data_t &&)                 = delete;
    // Data_t &operator=(const Data_t &) = delete;
    // Data_t &operator=(Data_t &&)      = delete;

    /**
     * @brief Constructs an id-only lookup operation.
     * @param[in] _id Client id to look up.
     */
    explicit Data_t(const uint64_t _id)
        : Entity_t('\0',
                   _id) {
    }  // Same sentinel trick as Delete_t; only
       // the id matters for the lookup.

    /**
     * @brief Destroys the id-only lookup operation.
     */
    ~Data_t() override = default;  // No resources to release beyond the base.
  };

  /**
   * @class Search_t
   * @brief Parsed and validated parameters for a paginated, case-insensitive
   * search.
   * @details Construction from a JSON body both extracts the fields and
   * reports, through the success flag, whether the body is well formed and
   * safe. filter and order_by end up as SQL identifiers in the generated
   * statement (they cannot be bound), so they are restricted to plain
   * identifiers; the remaining values are bound as parameters by the Entity
   * builders.
   * @see Entity::buildSearchStatement
   */
  using Search_t = struct Search_t {
    /**
     * @brief Substring to match (wrapped in %...% when bound).
     */
    std::string keyword;  // The text the user typed; it is a value and will
                          // be bound as a parameter.

    /**
     * @brief Column name used in the WHERE clause; must be a plain
     * identifier.
     */
    std::string filter;  // Written into SQL as an identifier, so it must
                         // pass isPlainIdentifier().

    /**
     * @brief Column name used in ORDER BY; must be a plain identifier.
     */
    std::string order_by;  // Also written into SQL, so it must pass
                           // isPlainIdentifier() too.

    /**
     * @brief Sort direction, "ASC" or "DESC", derived from the JSON
     * "direction" value.
     */
    std::string direction;  // Chosen from a numeric flag in the body, not
                            // taken directly from user text.

    /**
     * @brief Maximum number of rows requested (the builder binds limit + 1
     * for next-page detection).
     */
    size_t limit;  // A count, so an unsigned size_t is natural; the extra
                   // row indicates more results.

    /**
     * @brief Number of rows to skip.
     */
    size_t offset;  // Pagination skip count; also bound as a value.

    /**
     * @brief Parses a search JSON body and reports whether it can be used.
     * @param[in] search_j JSON body expected to contain keyword, filter,
     * order_by, direction, limit and offset.
     * @param[out] success Set true only when every field is present,
     * correctly typed and validated; callers use this to return a normal
     * client error instead of throwing. On any problem it is set false and
     * the remaining fields must not be used.
     */
    // The bool is an output parameter: parsing never
    // throws at the caller for a bad body.
    Search_t(const jsoncons::json& search_j,
             bool&                 success) {
      // A missing/malformed field here (e.g. an empty "{}" body) is a
      // client error, not a server error. success=false tells the caller
      // (EntityController::Search) to reply with a normal "Failed to
      // search" response instead of throwing - rethrowing after setting
      // success=false used to defeat that signal and escape uncaught into
      // the generic 500 handler, leaking the server's file path in the
      // response body.
      try {
        const jsoncons::json
            &kw = search_j.at("keyword"),
            &fl = search_j.at("filter"), &ob = search_j.at("order_by"),
            &dr = search_j.at("direction"), &lm = search_j.at("limit"),
            &of = search_j.at("offset");  // Grab references to all six fields;
                                          // .at throws if a key is missing.
        // jsoncons' as<T>() happily coerces across types (a JSON
        // bool/null becomes "true"/"" as a string, for instance)
        // instead of throwing, which let a body that violates the
        // declared schema (e.g. "order_by": false, "limit": null) sail
        // through this try block untouched. Check the actual JSON type
        // first so a mismatch is treated the same as any other
        // malformed search body - success=false, not silently-coerced
        // garbage.
        // Reject wrong JSON types before coercion
        // can hide them.
        if (!kw.is_string() || !fl.is_string() || !ob.is_string() ||
            !dr.is_int64() || !lm.is_int64() || !of.is_int64()) {
          success = false;  // Mark the body unusable.
          return;           // Stop early; the member fields are left unset but
                            // will not be read.
        }
        keyword   = kw.as<std::string>();  // Safe now that the type is
                                           // known to be a string.
        filter    = fl.as<std::string>();  // Safe string conversion.
        order_by  = ob.as<std::string>();  // Safe string conversion.
        direction = dr.as<short>() == 0
                        ? "ASC"
                        : "DESC";     // Map a numeric flag to a fixed SQL
                                      // keyword, never raw user text.
        limit     = lm.as<size_t>();  // Convert the numeric limit to a count.
        offset    = of.as<size_t>();  // Convert the numeric offset to a count.
        // Final gate: exact key set and
        // safe identifiers.
        // Covers a missing key (.at) or a
        // bad numeric conversion.
        success = validate(search_j);
      } catch (const std::exception&) {
        success = false;  // Any parsing problem means the body must not
                          // be used.
      }
    }
    // "filter" and "order_by" are written into the SQL text (identifiers
    // cannot be bound), so they must be plain identifiers: letters, digits
    // and underscores only, not starting with a digit.
    /**
     * @brief Checks that name is a safe SQL identifier
     * (letters/digits/underscore, not digit-first).
     * @details Enforces PostgreSQL's 63-character limit as well, because
     * the name is interpolated into the statement rather than bound.
     * @param[in] name Candidate column identifier.
     * @return true only when the name is non-empty, within the length limit
     * and plain.
     */
    // Static because it needs no object state; a
    // free-standing helper on the struct.
    static bool isPlainIdentifier(const std::string& name) {
      constexpr size_t MAX_IDENTIFIER_LENGTH =
          63;  // PostgreSQL's limit; longer names would be truncated.
      // Reject empty, too long, or starting with a digit
      // (which would be an invalid SQL identifier).
      if (name.empty() || name.size() > MAX_IDENTIFIER_LENGTH ||
          std::isdigit(static_cast<unsigned char>(name.front())) != 0) {
        return false;  // Unsafe name.
      }
      return std::ranges::all_of(name, [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
      });  // Every character must be a letter, digit
           // or underscore.
    }

    /**
     * @brief Verifies that the body has exactly the six search keys and
     * safe identifiers.
     * @param[in] search_j The original JSON body.
     * @return true when the key set is exact and filter/order_by are plain
     * identifiers.
     */
    // Non-static because it reads the
    // already-parsed member fields.
    bool validate(const jsoncons::json& search_j) {
      static const std::unordered_set<std::string> keys =
          {"keyword",
           "filter",
           "order_by",
           "direction",
           "limit",
           "offset"};  // The complete allowed key set, built once.
      return search_j.size() == keys.size() &&
             std::ranges::all_of(keys,
                                 [&search_j](const std::string& key) {
                                   return search_j.find(key).has_value();
                                 }) &&
             isPlainIdentifier(filter) &&
             isPlainIdentifier(
                 order_by);  // No extra or missing keys, and both
                             // identifiers are safe to paste into SQL.
    }
  };

  /**
   * @class ClientData_t
   * @brief Common parsing and field extraction for client-style create/update
   * payloads.
   * @details Parses a raw JSON body, records parse errors through
   * error/success, and keeps both the parsed JSON and a flat set of (column,
   * value) pairs used by the Client SQL builders. Password hashing on the
   * flat set is available to subclasses.
   * @see CreateClient_t, UpdateClient_t
   */
  struct ClientData_t {
   public:
    /**
     * @brief Parses the raw request body into data_j and reports status via
     * error/success.
     * @param[in] _data Raw JSON text of the request body.
     * @param[in] _id Optional client id (used by updates).
     * @param[out] error Receives a BAD_REQUEST status for an unparsable
     * body or an INTERNAL_SERVER_ERROR for an exception, with a diagnostic
     * message.
     * @param[out] success Set true only when parsing produced a JSON value.
     */
    ClientData_t(std::string_view              _data,
                 const std::optional<uint64_t> _id,
                 api::v2::Http::Error&         error,
                 bool&                         success)
        // string_view avoids copying the body; only the id is
        // stored here initially.
        : id(_id) {
      try {
        data_j = jsoncons::json::parse(
            _data);  // Turn the raw text into a JSON value.
        // A null result means the text was
        // not valid JSON.
        if (!data_j.has_value()) {
          error = {.code = api::v2::Http::Status::BAD_REQUEST,
                   .message =
                       "Failed to parse body."};  // A bad body is the
                                                  // client's fault (400).
          Message::ErrorMessage(error.message);   // Log it for operators.
          success = false;  // Tell the caller not to proceed.
          // Stop before the success = true at the end.
          // An unexpected exception while
          // parsing is a server-side problem.
          return;
        }
      } catch (const std::exception& e) {
        error =
            {.code = api::v2::Http::Status::INTERNAL_SERVER_ERROR,
             .message =
                 fmt::format("Failed while parsing client data: {}.",
                             e.what())};  // Report 500 with the exception text.
        success = false;                  // Mark failure.
        Message::CriticalMessage(error.message);  // Log at critical level.
        return;                                   // Stop early.
      }

      success = true;  // Reaching here means parsing produced a usable
                       // JSON value.
    }

    /**
     * @brief Returns the parsed body, if parsing succeeded.
     */
    const std::optional<jsoncons::json>& get_data_json() const {
      return data_j;
    }  // Const reference avoids copying; callers must check has_value()
       // first.

    /**
     * @brief Returns the optional client id supplied at construction.
     */
    std::optional<uint64_t> get_id() const {
      return id;
    }  // Return by value; callers check has_value() before using it.

   protected:
    /**
     * @brief Flattened (column, value) pairs derived from the validated
     * body.
     */
    std::unordered_set<std::pair<std::string, std::string>>
        data_set;  // Each pair is one SQL column and its value; a set
                   // prevents duplicates.

    /**
     * @brief The parsed JSON body, or nullopt if parsing failed.
     */
    std::optional<jsoncons::json> data_j;  // optional because parsing may fail.

    /**
     * @brief Optional client id.
     */
    std::optional<uint64_t> id;  // Present for updates, absent for creates.

    /**
     * @brief Replaces a plaintext "password" entry in data_set with its
     * hash.
     * @return true when a password was found and hashed; false when there
     * was no password or hashing failed. Logs a critical message on a
     * hashing failure in the caller path.
     * @note Operates on the flat pair set rather than data_j, so it must
     * run after validation has populated data_set.
     */
    // Protected so subclasses can choose whether to
    // hash (creates do, updates do not).
    bool hashPassword() {
      auto passwd_itr = std::ranges::find_if(data_set, [&](const auto& item) {
        return item.first == "password";
      });  // Look for a column named
           // "password" in the flat set.
      // Only act if such an entry exists.
      if (passwd_itr != data_set.end()) {
        std::string password_raw =
            passwd_itr->second;  // Copy the plaintext out before erasing it.
        std::optional<std::string> hashed_password =
            passwordCrypt->hashPassword(
                password_raw);  // Ask the shared hasher; may fail
                                // (nullopt).
        // Hashing failed - do not write a
        // plaintext password.
        if (!hashed_password.has_value()) {
          return false;  // Signal failure so the caller can react.
        }
        data_set.erase(passwd_itr);  // Remove the plaintext pair.
        data_set.emplace("password",
                         hashed_password
                             .value());  // Insert the hashed pair in its place.
        return true;                     // Report success.
      }
      return false;  // No password field existed; nothing to do.
    }

   private:
    /**
     * @brief Shared password hasher obtained from the global object store.
     */
    std::shared_ptr<PasswordCrypt> passwordCrypt =
        Store::cachedObject<PasswordCrypt>();  // One process-wide hasher;
                                               // shared_ptr prevents it being
                                               // destroyed while in use.
  };

  /**
   * @class CreateClient_t
   * @brief ClientData_t for creating a client: validates a create schema and
   * hashes any password.
   * @details Rejects a body that already contains "id" so clients cannot pick
   * their own key, runs regex validation to build data_set, then hashes the
   * password before the INSERT is built.
   */
  struct CreateClient_t : public ClientData_t {
   public:
    /**
     * @brief Parses and validates a client-create body, populating
     * data_set.
     * @param[in] _data Raw JSON request body.
     * @param[in] tablename Table whose create schema is used for
     * validation.
     * @param[out] error Receives a diagnostic on validation failure.
     * @param[out] success Set true only after schema and regex validation
     * succeed.
     */
    CreateClient_t(std::string_view      _data,
                   const std::string&    tablename,
                   api::v2::Http::Error& error,
                   bool&                 success)
        // Creates have no id yet, so pass nullopt to
        // the base.
        : ClientData_t(_data,
                       std::nullopt,
                       error,
                       success) {
      try {
        Validator::Rule rule(Validator::Rule::Action::ASSERT_NOT_PRESENT,
                             {"id"});  // Build a rule saying "id must not be in
                                       // the body".
        success = Validator::
            validateDatabaseCreateSchema(tablename,
                                         data_j,
                                         error,
                                         rule);  // Check the body against the
                                                 // table's create schema.
        // Schema validation failed.
        if (!success) {
          Message::ErrorMessage(error.message);  // Log the validation error.
          return;  // Leave success false and stop.
        }

        success =
            Validator::clientRegexValidation(data_j,
                                             error,
                                             data_set);  // Regex-validate
                                                         // fields and fill the
                                                         // (column, value) set.
        // Regex validation failed.
        if (!success) {
          Message::ErrorMessage(error.message);  // Log it.
          return;                                // Stop with success false.
        }
        // Now that data_set exists, replace any
        // plaintext password with its hash.
        // Unexpected error during validation.
        hashPassword();
      } catch (const std::exception& e) {
        error = {.code    = api::v2::Http::Status::INTERNAL_SERVER_ERROR,
                 .message = fmt::format("Failed while parsing client data: {}.",
                                        e.what())};  // Report 500.
        success = false;                             // Mark failure.
        Message::CriticalMessage(error.message);     // Log details.
        return;                                      // Stop.
      }

      success = true;  // Everything passed.
    }
    /**
     * @brief Returns the validated (and password-hashed) column/value pairs
     * for the INSERT.
     */
    const std::unordered_set<std::pair<std::string,
                                       std::string>>&
    get_data_set() const {
      return data_set;
    }  // Const reference so callers cannot mutate the validated data.
  };

  /**
   * @class UpdateClient_t
   * @brief ClientData_t for updating a client: validates an update schema
   * with a caller-supplied rule.
   * @details Unlike CreateClient_t it does not hash passwords; the validation
   * rule is provided by the caller because different update endpoints permit
   * different field sets.
   */
  struct UpdateClient_t : public ClientData_t {
   public:
    /**
     * @brief Parses and validates a client-update body, populating
     * data_set.
     * @param[in] _data Raw JSON request body.
     * @param[in] _id Client id whose row will be updated.
     * @param[in] tablename Table whose update schema is used for
     * validation.
     * @param[out] error Receives a diagnostic on validation failure.
     * @param[out] success Set true only after schema and regex validation
     * succeed.
     * @param[in] rule Validation rule (for example which fields may or may
     * not be present).
     * @note Unlike CreateClient_t this type does not re-hash the password:
     * the generic Update path marks `password` as ASSERT_IMMUTABLE, so no
     * new password can arrive here.
     */
    UpdateClient_t(std::string_view              _data,
                   const std::optional<uint64_t> _id,
                   const std::string&            tablename,
                   api::v2::Http::Error&         error,
                   bool&                         success,
                   const Validator::Rule&
                       rule)  // The extra rule parameter lets each endpoint
                              // decide which fields are allowed.
        // Pass the id through to the base for later
        // use by the UPDATE builder.
        : ClientData_t(_data,
                       _id,
                       error,
                       success) {
      try {
        success = Validator::
            validateDatabaseUpdateSchema(tablename,
                                         get_data_json(),
                                         error,
                                         rule);  // Validate against the table's
                                                 // update schema and the
                                                 // supplied rule.
        // Validation failed.
        if (!success) {
          Message::ErrorMessage(error.message);  // Log it.
          return;                                // Stop.
        }

        success = Validator::
            clientRegexValidation(data_j,
                                  error,
                                  data_set);  // Build the flat set with regex
                                              // checks, like the create path.
        // Regex validation failed.
        if (!success) {
          Message::ErrorMessage(error.message);  // Log it.
          // Stop.
          // Unexpected validation error.
          return;
        }
      } catch (const std::exception& e) {
        error = {.code    = api::v2::Http::Status::INTERNAL_SERVER_ERROR,
                 .message = fmt::format("Failed while parsing client data: {}.",
                                        e.what())};  // Report 500.
        success = false;                             // Mark failure.
        Message::CriticalMessage(error.message);     // Log details.
        return;                                      // Stop.
      }

      success = true;  // Everything passed.
    }
    /**
     * @brief Returns the validated column/value pairs for the UPDATE SET
     * clause.
     */
    const std::unordered_set<std::pair<std::string,
                                       std::string>>&
    get_data_set() const {
      return data_set;
    }  // Const reference keeps the validated data read-only.
  };

  /**
   * @struct SuspendData
   * @brief Carries the client id used by the suspend/activate statement
   * builders.
   */
  struct SuspendData {
    /**
     * @brief Id of the client whose active flag will be toggled.
     */
    uint64_t client_id;  // A plain id is enough here; there is always
                         // exactly one target.

    /**
     * @brief Stores the target client id.
     * @param[in] _client_id Id of the client to suspend or activate.
     */
    SuspendData(const uint64_t _client_id) {
      client_id = _client_id;
    }  // Copy the id into the member for the builder to read later.
  };

  /**
   * @struct StaffData
   * @brief Parsed staff-invitation data and rendering of the invitation link
   * payload.
   * @details Constructed from the JSON body of a staff add/remove request.
   * When parsed successfully it can also augment an invite JSON object with
   * the fields the mail template needs, including a base64-encoded link back
   * into the frontend.
   * @note parse_status must be checked before relying on the other fields; a
   * missing key leaves them uninitialised.
   */
  struct StaffData {
    /**
     * @brief Whether all required JSON fields were parsed successfully.
     */
    bool parse_status = false;  // success or failure to parse the json; callers
                                // must check this before using the fields.

    /**
     * @brief Id of the staff member being nominated.
     */
    uint64_t nominee_id;  // Set from JSON; bound into the staff-array SQL
                          // statement.

    /**
     * @brief Id of the entity (service/organisation) the staff member is
     * added to or removed from.
     */
    uint64_t entity_id;  // The row whose JSONB staff array is updated.

    /**
     * @brief Role assigned to the nominee (for example "Doctor").
     */
    std::string role;  // Human-readable role text from the body.

    /**
     * @brief Email address used in the invitation.
     */
    std::string email;  // Destination address for the invite.

    /**
     * @brief Group the nominee belongs to (for example "providers").
     */
    std::string nominee_group;  // Used when building the invitation context.

    /**
     * @brief Human-readable name of the entity.
     */
    std::string entity_name;  // Displayed in the invitation message.

    /**
     * @brief Project name injected from the server configuration.
     */
    std::string project_name;  // Not from the body; read from server config
                               // so the invite is branded correctly.

    /**
     * @brief Parses a staff JSON body, setting parse_status on success or
     * failure.
     * @param[in] json Body containing nominee_id, entity_id, role, email,
     * nominee_group and entity_name. project_name comes from configuration,
     * not from the body.
     * @note On a missing or mistyped key an error is logged and
     * parse_status is set false.
     */
    // Construct by extracting every
    // required field from the JSON body.
    StaffData(const jsoncons::json& json) {
      try {
        nominee_id = json.at("nominee_id")
                         .as<uint64_t>();  // .at throws if the key is missing;
                                           // as<> converts the value.
        entity_id =
            json.at("entity_id").as<uint64_t>();   // Same for the entity id.
        role = json.at("role").as<std::string>();  // Role name as text.
        // Email as text.
        email = json.at("email").as<std::string>();
        nominee_group =
            json.at("nominee_group").as<std::string>();  // Group name as text.
        entity_name =
            json.at("entity_name").as<std::string>();  // Entity name as text.
        project_name = servercfg_.name;  // Read from configuration,
                                         // never from client input.
        // All fields were read successfully.
        // A missing or mistyped key lands here.
        parse_status = true;
      } catch (const std::exception& e) {
        Message::ErrorMessage(
            "Error while parsing json: " +
            std::string(e.what()));  // Log the parsing problem.
        parse_status = false;        // Warn callers not to use the other
                                     // (possibly unset) fields.
      }
    }

    /**
     * @brief Adds the fields required by the invitation email template to
     * invite_json.
     * @details The current JSON is base64-encoded into the frontend invite
     * link so the invite payload travels as a single URL-safe token;
     * subject, template, project name and a generate_body placeholder are
     * added alongside it.
     * @param[in,out] invite_json Invitation JSON to augment in place.
     * @return true when the fields were added; false if an exception
     * occurred.
     */
    // Takes the JSON by reference so it
    // is modified in place.
    bool toInviteJson(jsoncons::json& invite_json) {
      try {
        std::string encoded_invite_data = cppcodec::base64_rfc4648::encode(
            invite_json.to_string());  // Base64 makes the JSON safe
                                       // to place in a URL token.
        invite_json["subject"] =
            fmt::format("Invite to {}",
                        project_name);  // Email subject built from the
                                        // configured project name.
        invite_json["template"] =
            "invite_staff_to_entity.txt";  // Names the mail template
                                           // file to use.
        invite_json["link"] =
            fmt::format("{}:{}/{}/{}",
                        frontendcfg_.host,
                        frontendcfg_.port,
                        frontendcfg_.invite_path,
                        encoded_invite_data);  // Frontend URL carrying the
                                               // encoded token.
        invite_json["project_name"] =
            project_name;  // Pass the project name to the template.
        invite_json["generate_body"] =
            "";  // Empty placeholder the mail layer fills in.
        // All fields added.
        // Encoding or formatting failed.
        return true;
      } catch (const std::exception& e) {
        Message::ErrorMessage("Error while creating invite json: " +
                              std::string(e.what()));  // Log the failure.
        return false;  // Signal that the invite JSON is incomplete.
      }
    }
    /**
     * @brief Default construction; leaves parsed fields unset and
     * parse_status false.
     */
    StaffData() = default;  // Needed by code that creates an empty
                            // StaffData before filling it.

   private:
    /**
     * @brief Shared configurator used to read server and frontend settings.
     */
    const std::shared_ptr<Configurator> cfg_ =
        Store::cachedObject<Configurator>();  // One cached configurator
                                              // shared by all StaffData
                                              // objects.

    /**
     * @brief Server configuration, source of project_name.
     */
    const Configurator::ServerConfig& servercfg_ =
        cfg_->get<Configurator::ServerConfig>();  // A reference to the
                                                  // cached server settings;
                                                  // no copy.

    /**
     * @brief Frontend configuration, source of the invitation link
     * host/port/path.
     */
    const Configurator::FrontEndConfig& frontendcfg_ =
        cfg_->get<Configurator::FrontEndConfig>();  // A reference to the
                                                    // cached frontend
                                                    // settings for link
                                                    // building.
  };

  /**
   * @brief The set of operation payloads an Entity may hold; the active
   * alternative selects the builder.
   * @details Besides the CRUD types it includes client/staff/suspend payloads
   * consumed by the specialised Client and Service builders.
   */
  using EntityType =
      std::variant<Create_t,
                   Read_t,
                   Update_t,
                   Delete_t,
                   Data_t,
                   Search_t,
                   CreateClient_t,
                   UpdateClient_t,
                   SuspendData,
                   StaffData>;  // One variant lists every payload an entity can
                                // carry; std::variant remembers which type is
                                // currently stored.
}  // namespace api::v2::Types
