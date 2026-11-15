# Remaining Work

> Re-verified against the code at `b8790f7` on 2026-11-13. The release pipeline works: tag builds pass, and the multi-arch image is published to Docker Hub.

## 1. Security

- [ ] **Logout doesn't verify the token.** `jwt::decode` doesn't check the signature, and `ValidateToken` is commented out, so a forged token can log any `clientId` out. `src/gatekeeper/sessionmanager/sessionmanager.cpp:102-118`. Also, `token.value()` is called unchecked (`:100`): with no token the client gets a `500` whose body is the raw `bad_optional_access` text (the unit test "logout with a bad token" hits this).
- [ ] **The auth filter accepts a forged token when the client has a session.** `SessionManager::clientHasValidSession` (`src/gatekeeper/sessionmanager/sessionmanager.cpp:156-186`) decodes the JWT without verifying the signature and looks the session up by `group_id`; it never compares the token with the one stored at login. The signature is only verified on a session miss. Look sessions up by a hash of the token (or compare it), and add a forged-token test. See Step 8 of `PERFORMANCE_PLAN.md`.
- [ ] **Whitelist and blacklist rework** (`src/gatekeeper/dosdetector/dosdetector.cpp:67-90`, `:219-275`). Open decisions: should the blacklist win, and is `X-Forwarded-For` support needed?
  - **Matching is far too broad.** Entries are regexes passed to `std::regex_search`: unanchored, with `.` matching any character. `192.168.1.*` also matches `192.168.10.4` and `192.168.199.1`; `127.0.0.1` matches `127.0.0.10`-`.199`. A whitelisted IP skips all DoS checks, so every over-match is a bypass. Fix: compile entries into exact-IP, CIDR (`192.168.1.0/24`) and per-octet wildcard (`192.168.1.*`) matchers; optionally keep regex behind a `re:` prefix.
  - **The whitelist overrides the blacklist.** It's checked first, and the test `whitelist wins over blacklist` locks that in. Decide whether the deny list should win; if not, document the current order.
  - **The client IP is wrong behind Docker or a proxy.** `req->peerAddr()` (`src/api/v2/filters/ratelimit.hpp:27`) is the Docker gateway (`172.17.0.1`, `192.168.64.1`) or the proxy, so every client shares one IP: one noisy client gets everyone limited, and whitelisting it whitelists everyone. Add opt-in trusted-proxy handling of `X-Forwarded-For`.
  - **Tests:** add over-match cases (`192.168.1.50`, `127.0.0.10`), CIDR/wildcard cases, and blacklist-versus-whitelist precedence. Update the invalid-pattern test.
  - **Cleanup:** collapse the three nested `try/catch` layers (`isWhitelisted` → `regexFind` → the lambda). `tests/README.md` item 9 (`"a,,b"` gives an empty match-everything entry) is outdated, because `parseSet` now skips empty items.
- [ ] **The startup log prints the database password and the JWT secret.** `src/configurator/configurator.hpp:106`, `:187`
- [ ] **Nothing stops a native run with the default `TOKEN_SECRET`.** (The Docker image already generates a random one.) Warn or refuse when it equals `Defaults::TokenParameters::TOKEN_SECRET_`.
- [ ] **Duplicate JSON keys.** The `hasDuplicateKeys` check is commented out on the assumption that jsoncons rejects duplicates. Verify that's true, and enable strict parsing if it isn't. `src/validator/validator.cpp:29-34`, `:82-86`

## 2. Bugs

- [ ] **Adding staff updates the wrong row.** `getSqlAddStaffStatement` passes 4 arguments to a format string with 3 placeholders, so `WHERE ID=` gets `nominee_group` (e.g. `providers`) instead of `entity_id`, and the query fails against a real database. Drop `idata.nominee_group` from the arguments. The test "builds an UPDATE with the correct table and id" never checks the id; make it assert `WHERE ID=7`. `src/entities/base/service.hpp:37-42`, `tests/entities/test_entities.cpp:330-340`
- [ ] **`communicate.cpp` sends stale data.** The `static` method map's lambdas capture `curl` **and `data`** by reference on the first call. Every later request uses dangling references, including the first call's request body. Build the map without captures, or set the options directly. `src/utils/communicate/communicate.cpp:24-30`
- [ ] **Deleting a client removes the session unconditionally,** even when the delete failed. `src/controllers/clientcontroller/clientcontroller.cpp:122-123`
- [ ] **Rate-limit filter replies.** A `RATELIMITED` reply says "IP is blacklisted." (`:45`), and `BANNED` returns `503 Service Unavailable` (`429` or `403` fits better). `src/api/v2/filters/ratelimit.hpp`
- [ ] **Appointment permissions.** Update and delete checks use the _read_ permission query, and appointment entities define no update or delete queries. Decide the intended rule. `src/gatekeeper/permissionmanager/permissionmanager.cpp:159-179`
- [ ] **The client validator coerces before checking type.** `as<std::string>()` turns numbers and booleans into strings before the regex checks, so a numeric `phone` passes. `src/validator/validator.cpp:169`

## 3. API conformance (Schemathesis)

- [ ] **ST201: search integers and extra keys.** `is_number()` accepts `2.5`, and `as<size_t>()` truncates it. Unknown properties are also accepted, though the schema has `additionalProperties: false`. Reject both, and add unit tests. `src/entities/base/types.hpp` (`Search_t`)
- [ ] **ST204: `Allow` header.**
  - Every route gets the same hard-coded `Allow` list.
  - The `405` list omits `HEAD`, while `addCorsHeaders` adds `Allow` (with `HEAD`) to **every** response.
  - Fix: make the two lists consistent, and stop sending `Allow` on normal responses.

  `src/server/server.cpp:69-71`, `:115`

- [ ] **Re-run the conformance suite** and confirm zero ST201 and ST204 failures.

## 4. Concurrency (low risk)

- [ ] **Race in `Store`.** `getObject`/`registerObject` check and insert under separate locks; two first-time callers can race (the second throws "already registered"). `src/store/store.hpp:24-56`
- [ ] **`MemCache` only expires entries from the back of the list,** so an entry with an earlier expiry that was moved to the front survives until everything behind it has expired. `src/utils/memcache/memcache.hpp:205-207`

## 5. Code quality

- [ ] **Dead code:**
  - `ResetPassword` (an empty stub that no route calls; `src/controllers/clientcontroller/clientcontroller.cpp:205-209`)
  - unreachable `return true` after a returning `catch` (`src/validator/validator.cpp:71`, `:114`)
  - unused `dos_detector` member (`src/api/v2/filters/ratelimit.hpp:60`)
  - redundant key loop in `Search_t::validate` (`types.hpp:147-151`)
- [ ] **Asymmetric rule-of-five.** Copy-construct is allowed but copy-assign is deleted. `KeeprBase` (`keeprbase.hpp:22-23`), `SessionManager` (`sessionmanager.hpp:22-24`)
- [ ] **Macros:**
  - `IUGUARD` declares a named local (`src/database/database.cpp:22`)
  - `CRITICALMESSAGE` and `CRITICALMESSAGERESPONSE` depend on `e` and `callback` being in scope (`src/utils/global/global.hpp`)
- [ ] **Typos in names:**
  - `DatabaseHanndler` (4 files)
  - directory `sqlinjectiondetectoor/`
  - `staffcontrollerBase.hpp` (mixed case)
- [ ] **`StaffData` logs with `fmt::print` and no newline,** so its parse errors bypass the logger and run into the next log line. Use `Message::ErrorMessage`. `src/entities/base/types.hpp:316`
- [ ] **Style and portability:**
  - `__attribute((always_inline))` in 3 files (use `[[gnu::always_inline]]`)
  - `SuspendData` doesn't use an initializer list (`types.hpp:287`)
  - `using X = struct X` config structs (`configurator.hpp`)
  - hidden `static` `db_ctl` cache (`permissionmanager_private.hpp:79`)
  - 5 `// [ ]` TODO comments
  - `Store::getObject` called in constructors, so initialization order depends on first use

## 6. Documentation

- [ ] **The OpenAPI generator is stale.** It shows the old staff URL (`/services/{type}/staff/...`, really `/staff/services/{type}/staff/...`) and clinic-only appointments. Update `tools/docs/build_docs.py:375-389` and `tools/docs/steps.py:66-93`, run `tools/docs/build.sh`, and commit `docs/`.
- [ ] **Check the staff URLs against a running server.** The README documents them from the code.

## 7. Build

- [ ] **Confirm a full image build with `conan.lock`.** The lock is in place but no image has been built with it yet. The next deps image build does it (ccache-warm on a push to main that changes the Conan files, or a manual `deps.yml` run), and it recompiles every dependency once, because `conan.lock` changes the cached dependency layer.
- [ ] **Release a new image.** The entrypoint now handles persistence (loads the dump at start instead of `init.sql`, saves it on stop, `SIGHUP` restarts only the server) and the Dockerfile stamps the real version. Both reach users only with the next tagged release. Until then `valhalla.sh` falls back to restoring dumps itself. `docker/entrypoint.sh`, `docker/Dockerfile`

---

## Known issue, accepted as is

- **Port already in use.** Trantor `exit(1)`s while threads are live, and the process aborts with "mutex lock failed".
