# Remaining Work

> Re-verified against the code at `b8790f7` on 2026-11-13. The release pipeline works: tag builds pass, and the multi-arch image is published to Docker Hub.

## 1. Security

- [ ] **Logout doesn't verify the token.** `jwt::decode` doesn't check the signature, and `ValidateToken` is commented out, so a forged token can log any `clientId` out. `src/gatekeeper/sessionmanager/sessionmanager.cpp:102-118`
- [ ] **Whitelist and blacklist rework** (`src/gatekeeper/dosdetector/dosdetector.cpp:67-90`, `:219-275`). Open decisions: should the blacklist win, and is `X-Forwarded-For` support needed?
  - **Matching is far too broad.** Entries are regexes passed to `std::regex_search`: unanchored, with `.` matching any character. `192.168.1.*` also matches `192.168.10.4` and `192.168.199.1`; `127.0.0.1` matches `127.0.0.10`-`.199`. A whitelisted IP skips all DoS checks, so every over-match is a bypass. Fix: compile entries into exact-IP, CIDR (`192.168.1.0/24`) and per-octet wildcard (`192.168.1.*`) matchers; optionally keep regex behind a `re:` prefix.
  - **The whitelist overrides the blacklist.** It's checked first, and the test `whitelist wins over blacklist` locks that in. Decide whether the deny list should win; if not, document the current order.
  - **Every request pays for it.** `regexFind` builds a new `std::regex` per entry per request, for both lists, while holding `whitelist_mutex_`/`blacklist_mutex_`, so all worker threads queue on one lock and benchmarks suffer. The lists are `const` after startup: compile them once in the constructor and remove both mutexes.
  - **Invalid patterns are logged on every request,** never once at startup. The test `ignores invalid whitelist regex patterns` codifies that. Validate at startup instead.
  - **The client IP is wrong behind Docker or a proxy.** `req->peerAddr()` (`src/api/v2/filters/ratelimit.hpp:27`) is the Docker gateway (`172.17.0.1`, `192.168.64.1`) or the proxy, so every client shares one IP: one noisy client gets everyone limited, and whitelisting it whitelists everyone. Add opt-in trusted-proxy handling of `X-Forwarded-For`.
  - **Tests:** add over-match cases (`192.168.1.50`, `127.0.0.10`), CIDR/wildcard cases, and blacklist-versus-whitelist precedence. Update the invalid-pattern test.
  - **Cleanup:** collapse the three nested `try/catch` layers (`isWhitelisted` → `regexFind` → the lambda). `tests/README.md` item 9 (`"a,,b"` gives an empty match-everything entry) is outdated, because `parseSet` now skips empty items.
- [ ] **The SQL-injection check scans the parameterized query** (`$1`, `$2`), which never contains user values. `src/database/database.cpp:185`, `:281`
- [ ] **Password hashing uses scrypt's `OPSLIMIT_MIN`/`MEMLIMIT_MIN`,** which are test-only strengths. Use `*_INTERACTIVE` or stronger. `src/gatekeeper/passwordcrypt/passwordcrypt.cpp:34-35`
- [ ] **The startup log prints the database password and the JWT secret.** `src/configurator/configurator.hpp:106`, `:187`
- [ ] **Nothing stops a native run with the default `TOKEN_SECRET`.** (The Docker image already generates a random one.) Warn or refuse when it equals `Defaults::TokenParameters::TOKEN_SECRET_`.
- [ ] **Duplicate JSON keys.** The `hasDuplicateKeys` check is commented out on the assumption that jsoncons rejects duplicates. Verify that's true, and enable strict parsing if it isn't. `src/validator/validator.cpp:29-34`, `:82-86`

## 2. Bugs

- [ ] **`communicate.cpp` sends stale data.** The `static` method map's lambdas capture `curl` **and `data`** by reference on the first call. Every later request uses dangling references, including the first call's request body. Build the map without captures, or set the options directly. `src/utils/communicate/communicate.cpp:24-30`
- [ ] **Exceptions swallowed without a response.** `GetServices`, `GetVisits` and `cruds` catch with `CRITICALMESSAGE`, which never calls `callback`, so the request is never answered. Use `CRITICALMESSAGERESPONSE`. `src/controllers/base/controller/controller.hpp:193`, `:224`, `:356`
- [ ] **Deleting a client removes the session unconditionally,** even when the delete failed. `src/controllers/clientcontroller/clientcontroller.cpp:122-123`
- [ ] **Copying a `DatabaseHanndler`** shares `db_ptr`, and both destructors return the same connection to the pool. Delete its copy operations. `src/database/databasehandler.hpp:12-14`
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
- [ ] **Pool size read without its lock** (startup log only). `src/database/databaseconnectionpool.cpp:88`

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

---

## Known issue, accepted as is

- **Port already in use.** Trantor `exit(1)`s while threads are live, and the process aborts with "mutex lock failed".
