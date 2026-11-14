# Remaining Work

> Re-verified against the code at `b8790f7` on 2026-11-13. The release pipeline works: tag builds pass, and the multi-arch image is published to Docker Hub.

## 1. Security

- [ ] **Logout doesn't verify the token.** `jwt::decode` doesn't check the signature, and `ValidateToken` is commented out, so a forged token can log any `clientId` out. `src/gatekeeper/sessionmanager/sessionmanager.cpp:102-118`
- [ ] **Whitelist and blacklist matching is unanchored.** Entries are regexes passed to `std::regex_search`, so a whitelist entry `10.0.0.1` also exempts `10.0.0.123`. Use exact or CIDR matching, or anchor each pattern automatically. `src/gatekeeper/dosdetector/dosdetector.cpp:247`
- [ ] **The SQL-injection check scans the parameterized query** (`$1`, `$2`), which never contains user values. `src/database/database.cpp:185`, `:281`
- [ ] **Password hashing uses scrypt's `OPSLIMIT_MIN`/`MEMLIMIT_MIN`,** which are test-only strengths. Use `*_INTERACTIVE` or stronger. `src/gatekeeper/passwordcrypt/passwordcrypt.cpp:34-35`
- [ ] **The startup log prints the database password and the JWT secret.** `src/configurator/configurator.hpp:106`, `:187`
- [ ] **Nothing stops a native run with the default `TOKEN_SECRET`.** (The Docker image already generates a random one.) Warn or refuse when it equals `Defaults::TokenParameters::TOKEN_SECRET_`.
- [ ] **Duplicate JSON keys.** The `hasDuplicateKeys` check is commented out on the assumption that jsoncons rejects duplicates. Verify that's true, and enable strict parsing if it isn't. `src/validator/validator.cpp:29-34`, `:82-86`

## 2. Bugs

- [ ] **`communicate.cpp` sends stale data.** The `static` method map's lambdas capture `curl` **and `data`** by reference on the first call. Every later request uses dangling references, including the first call's request body. Build the map without captures, or set the options directly. `src/utils/communicate/communicate.cpp:24-30`
- [ ] **The curl header list is never freed** on any path. `curl_slist_free_all(headers)` is missing. `src/utils/communicate/communicate.cpp:15`
- [ ] **Exceptions swallowed without a response.** `GetServices`, `GetVisits` and `cruds` catch with `CRITICALMESSAGE`, which never calls `callback`, so the request is never answered. Use `CRITICALMESSAGERESPONSE`. `src/controllers/base/controller/controller.hpp:193`, `:224`, `:356`
- [ ] **Deleting a client removes the session unconditionally,** even when the delete failed. `src/controllers/clientcontroller/clientcontroller.cpp:122-123`
- [ ] **`buildReadStatement` logs a missing `user_id`, then calls `.value()` anyway** (throws `bad_optional_access`). Return `std::nullopt`. `src/entities/base/entity.hpp:157-161`
- [ ] **Copying a `DatabaseHanndler`** shares `db_ptr`, and both destructors return the same connection to the pool. Delete its copy operations. `src/database/databasehandler.hpp:12-14`
- [ ] **Wrong rate-limit message.** A `RATELIMITED` reply says "IP is blacklisted." `src/api/v2/filters/ratelimit.hpp:45`
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
  - comment `DODDetectorConfig` (`src/configurator/configurator.cpp:4`)
  - banner comment says `[1, size-1]`, but the code uses `[0, …]` (`src/server/extras/banner.hpp:48`)
- [ ] **Includes:**
  - `database.cpp` includes `staffcontroller.hpp` for nothing (`:18`)
  - unused `<iostream>`/`<ostream>` in `sessionmanager.cpp` (`:12`, `:15`)
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

- [x] **Add a Conan lockfile.** Run `conan lock create .`, commit `conan.lock`, add `--lockfile` to `docker/Dockerfile`, and pin Conan exactly instead of `conan>=2,<3`. Without it, ConanCenter recipe updates can break a release unexpectedly.
  - *Done:*
    - `conan.lock` pins 20 libraries and 12 build tools, merged from Alpine amd64, Alpine arm64, and local Release/Debug.
    - Docker uses `--lockfile=conan.lock --lockfile-partial`, so a newer Alpine CMake from a base-image bump doesn't break the build.
    - Conan is pinned to `2.33.0`.
    - `tools/conan/update_lock.sh` regenerates the lock for all platforms; the README explains when to use it.
  - *Verified:* the lock resolves strictly for Linux x86_64/armv8 (GCC 15), macOS Release/Debug, and Ubuntu GCC 13 Debug. Inside real Alpine 3.24 (partial) it resolves to the same pinned recipe revisions. The script reproduces an identical lock.
  - *Not yet run:* a full image build with the lock. The next tag build will do it, and it will recompile every dependency once, because `conan.lock` changes the cached dependency layer.

---

## Known issue, accepted as is

- **Port already in use.** Trantor `exit(1)`s while threads are live, and the process aborts with "mutex lock failed".
