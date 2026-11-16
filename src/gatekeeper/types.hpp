/**
 * @file types.hpp
 * @brief Login, credential and identity data types used by the gatekeeper
 * subsystem.
 * @details This header defines two small plain-data structs that act as
 * "containers" passed between the login, session and token helpers. Credentials
 * holds the raw username/password typed by a client. ClientLoginData is a
 * larger bag of optional fields that grows as a login or authentication request
 * moves through the system: some fields come from the request, some from the
 * database, and some are produced when a token is created. Nothing here does
 * any work; it just gives the different helpers a shared vocabulary for the
 * data they exchange.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This file belongs to the gatekeeper (the security/identity part of the
 * server). GateKeeper reads the request body into Credentials, then creates a
 * ClientLoginData and passes it to SessionManager (session database rows) and
 * TokenManager (JWT creation/validation). Almost every security function takes
 * a ClientLoginData by reference, so without these types the helpers would need
 * long, duplicated parameter lists and could not share state. Fields are
 * optional because they are filled in at different stages - a field that is
 * `nullopt` simply means "not known yet" at this stage of the pipeline.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - struct: https://en.cppreference.com/w/cpp/language/classes
 * - std::optional ("a value that may be missing"):
 * https://en.cppreference.com/w/cpp/utility/optional
 * - std::chrono (time durations/time points):
 * https://en.cppreference.com/w/cpp/chrono
 * - JWT (a signed token carrying identity claims): https://jwt.io/introduction
 * - std::uint64_t (a fixed 64-bit whole number):
 * https://en.cppreference.com/w/cpp/types/integer
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once
// Needed for expireTime, which stores a time as a system_clock duration.
#include <chrono>
// Needed for uint64_t, the fixed-width id type used by clientId.
#include <cstdint>
// Needed for std::optional - most fields may be absent until filled in.
#include <optional>
// Needed for the text fields (token, username, group, ip, timestamps).
#include <string>

namespace api::v2::Types  // Groups these types under the "api version 2 /
                          // Types" namespace so names do not clash.
{
  /**
   * @struct ClientLoginData
   * @brief Mutable accumulator that carries a client's identity through login
   * and authentication.
   * @details The same object is passed from GateKeeper into SessionManager,
   * TokenManager and KeeprBase helpers. Fields are optional because they are
   * populated at different stages: some arrive from the request (group,
   * ip_address), some are read from the database (is_active, lastLogoutTime),
   * and some are produced by token generation (token, expireTime). Call sites
   * value-initialize this struct (for example `ClientLoginData{}`), so the
   * non-optional scalar members start zero-initialized and are only
   * meaningful once the corresponding helper sets them.
   * @note `passwordHash` and `passwordMatch` are declared here but are never
   * assigned in src/; the login flow carries the hash as a separate local
   * value.
   * @note Identity claims (username, clientId, group, ip_address,
   * lastLogoutTime) are bound into the JWT and re-checked on validation, so
   * changing one invalidates the token.
   * @details In everyday terms, think of this struct as a "passport
   * application form" that gets stamped at each desk. It begins nearly empty,
   * the HTTP request fills in the group and IP, the database fills in the
   * active flag and last logout time, and the token generator fills in the
   * token and its expiry. `std::optional` is used for each field because "not
   * filled in yet" is a normal state, not an error, and it lets helpers check
   * `has_value()` before using a field. The two plain `bool` fields are not
   *          optional, so they start as `false` after `ClientLoginData{}` and
   * must be explicitly set from the database before they mean anything.
   */
  using ClientLoginData = struct ClientLoginData
  {
    std::optional<std::string>
        token; /**< JWT issued on login; supplied by the client on
                  subsequent requests. Optional because it only exists after
                  token creation, and later requests send it back. */
    std::optional<std::string>
        username; /**< Authenticated user name, used as the JWT subject.
                     Optional until the database lookup or token decode
                     supplies it. */
    std::optional<std::string>
        group; /**< Client group ("users" or "providers"); selects the
                  session table and becomes a JWT claim. Optional until the
                                         request or token says which group.
                */
    std::optional<uint64_t>
        clientId; /**< Numeric client id, used as the JWT "jti" claim. A
                     64-bit unsigned number so it can hold large database
                     ids; optional until resolved. */
    std::optional<std::string>
        lastLogoutTime; /**< Last logout timestamp from the session table,
                           mirrored as the "llodt" claim to detect later
                           logouts. Stored as text because that is how it is
                           kept in the database. */
    std::optional<std::string>
        nowLoginTime;   /**< Timestamp of the login currently being processed,
                           written to the session row. Optional until the
                           login helper generates the current UTC time. */
    bool is_active;     /**< Account active flag read from the client table;
                           gates token validation. A plain bool (zero-initialized
                           to false) so a suspended account fails closed. */
    bool passwordMatch; /**< Declared but never assigned in src/; reserved
                           for login state. Kept as a plain bool for future
                           use. */
    std::optional<std::string>
        passwordHash; /**< Declared but never assigned in src/; the hash is
                         passed as a local value. Optional because this
                         struct does not actually carry it in the current
                         flow. */
    std::chrono::system_clock::duration
        expireTime; /**< Token expiry as a system_clock duration, compared
                       against the current time during decode. A duration
                       (not a string) makes the "now >= expiry" comparison
                       easy and exact. */
    std::optional<std::string>
        ip_address; /**< Client IP bound into the token and compared against
                       the request IP on validation. Optional until the
                       filter supplies it; binding it stops a stolen token
                       being reused elsewhere. */
  };

  /**
   * @struct Credentials
   * @brief Raw username/password pair parsed from a login request body.
   * @details Held only until the slow password work is dispatched to the
   * password worker.
   * @details These are ordinary non-optional strings because a login request
   * must have both values for parsing to succeed. The check that both keys
   * exist is done by GateKeeper::parse_credentials before this struct is
   * built, so by the time a Credentials object exists its fields are known to
   * be present. It is a small value type on purpose: it gets moved into a
   * lambda that runs on a worker thread, so keeping it cheap to copy/move
   * avoids extra work.
   */
  using Credentials = struct Credentials
  {
    std::string
        username;         /**< The user's username. Plain string because it is
                             required; used to look up the stored password hash. */
    std::string password; /**< The user's password. Plain string because it is
                             required; compared against the stored hash on the
                             password worker, never stored here long-term. */
  };
}  // namespace api::v2::Types
