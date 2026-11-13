# Valhalla-server unit-test suite (Catch2 v3)

Catch2-based, hermetic unit tests. **No PostgreSQL, no network, no real
filesystem outside a temp dir, no sleeps on real time, no dependency on `.env`,
hostname, locale or the current working directory.**

## How to run

```bash
# first time (or after dependency changes)
conan install . --build=missing -s build_type=Debug

cmake -S . -B build/Debug -DCMAKE_BUILD_TYPE=Debug -G Ninja
cmake --build build/Debug
ctest --test-dir build/Debug --output-on-failure      # every TEST_CASE = 1 ctest entry
ctest --test-dir build/Debug -N                       # list all discovered tests

# only fast tests, skip the hidden opt-in ones
ctest --test-dir build/Debug --output-on-failure -LE .integration
```

Run a single test:

```bash
build/Debug/tests/unit_tests "DoSDetector rate-limits an IP whose distinct request fingerprints exceed MAX_FPS"
```

Notes:

- `tools/conan/variables.cmake` sets `BUILD_TESTING OFF` (for third-party
  dependencies). The root `CMakeLists.txt` calls `enable_testing()` before
  `add_subdirectory(tests)` so CTest actually discovers the suite.
- Conan's CMakeDeps generator does not install the CMake helper that defines
  `catch_discover_tests()`; it is vendored verbatim from the Catch2 3.7.1 Conan
  package into `tests/cmake/` (BSD 3-Clause).
- `-DENABLE_TESTS=OFF` skips the whole suite (useful for CI release builds).
- On Linux Debug builds, ASan/UBSan/LSan are applied globally (server, core and
  tests). On macOS Debug they are not (mirrors the original per-target setup).

## Layout

```
tests/
├── CMakeLists.txt
├── README.md          # this file: plan, how to run, conventions
├── cmake/             # vendored Catch2 CMake helpers (catch_discover_tests)
├── support/           # shared helpers: env guards, fake clock, builders
├── gatekeeper/        # dos, sqli, password, token, session, permission, keeperbase
├── configurator/      # envloader / configurator / defaults
├── validator/         # rules, regex validation, search data
├── utils/             # memcache, threadsafequeue, jsonhelper, message, http, requester
├── entities/          # SQL statement builders, entity types
└── api/               # drogon response helpers (pure response building)
```

File naming: `test_<unit>.cpp`. Tags follow the convention below.

## Testability assessment

| Area | Coupling | Notes |
|---|---|---|
| `SqlInjectionDetector` | static state only | pure logic; static `suspiciousPatterns`/`riskyKeywords`/`customPatterns` persist across tests (kept in one TU, ordered) |
| `PasswordCrypt` | libsodium | pure, no globals |
| `PermissionManager` (client paths) + `PermissionManagerPrivate` | none | pure functions over `Requester` + `jsoncons::json` |
| `EnvLoader`/`EnvVars`/`Configurator::*Config` | `getenv` | tests use an RAII env guard (`tests/support/env_guard.hpp`) |
| `MemCache`, `ThreadSafeQueue` | internal threads | cache cleaner uses real clock; tests avoid real-time expiry waits |
| `DOSDetector` | clock, env config, cleanup thread | clock + config injected via new test constructor (Phase C) |
| `TokenManager`, `KeeprBase` | `Store` singletons, DB | token *issuance* is DB-free; *validation* needs DB (gap) |
| `SessionManager` | DB via `KeeprBase`, `MemCache` | store/remove/lookup are DB-free with lazy pool (Phase C) |
| `Validator::Rule`, `clientRegexValidation` | none | pure |
| `Validator::validateDatabase*Schema` | `DatabaseSchema` static map | map is only populated from a live DB (gap; see known bugs) |
| `DatabaseController`/pool | PostgreSQL | pool construction would `exit()` without a DB — made lazy (Phase C) |
| Controllers, DB layer, Drogon filters success paths | Drogon app / DB | out of scope for unit tests (documented below) |
| `Server` bootstrap | starts Drogon app | out of scope |

`Store` is a type-indexed singleton registry: tests that exercise singletons
(`Configurator`, `DOSDetector`, `SessionManager`, ...) must set the environment
they need **before** the first `Store::getObject<...>()` creates the object. The
suite does this in a global Catch2 listener (`tests/support/global_env.hpp`) so
order is irrelevant.

## Deliberately not unit-tested (and why)

- **`src/server/`** — bootstrap; starts the Drogon app loop.
- **`src/database/`** — real PostgreSQL only. Pool construction, watchdog and
  query execution are skipped; `InUseGuard` RAII *is* tested (pure
  synchronisation).
- **Controllers & DB-backed control flow** — every path funnels through
  `DatabaseController` which requires a live DB; a narrow fake seam would be a
  large refactor (see gaps).
- **`KeeprBase::validateToken` / `SessionManager::login`** — need the DB to
  fetch `last_logout` / the password hash.
- **Drogon filters (`Auth`, `RateLimit`) success paths** — `fccb()` requires a
  live Drogon handler chain. Rejection paths that build a response are tested.
- **`Communicate`** — raw libcurl; network-free unit surface is empty.
- **`Logger`** — only tested as "constructs with a temp log dir and creates the
  file"; `spdlog::shutdown()` in a test would poison the global async logger
  used by the rest of the suite.

## Known production bugs / surprising behaviour (found while testing)

Kept as `[.known-bug]` `[!shouldfail]` tests or documented here. **Not silently
fixed.**

1. **`sqlinjectiondetector.cpp` `initialize()`** — declares a *local*
   `suspiciousPatterns` vector that shadows the static member, so none of the
   ~35 built-in regex patterns are ever installed. `containsSuspiciousPattern`
   only ever sees custom patterns. Fix: drop the local `std::vector<std::regex>`
   qualifier (assign to the member).
2. **`sqlinjectiondetector.cpp` `hasCommentTokens` / `hasRiskyKeywords`** —
   flags every `--`, `/*`, `#` and whole words such as `SYSTEM`, `SLEEP`,
   `DELAY`, `EXECUTE` anywhere in a query. Benign text ("immune system",
   "patient delay", `--` as dash, `#hashtag`) is rejected; `O'Brien` trips the
   unbalanced-quote check. Documented with `[!shouldfail]` cases.
3. **`validator.cpp` `getDatabaseSchemaForTable`** — sets `found = false` in the
   not-found branch but then unconditionally `found = true` before returning;
   worse, on a table miss it *dereferences the end iterator*
   (`table_schema_it->second`). Fix: early-return with `found = false` (and
   return `{}`) when the table is missing.
4. **`keeprbase.cpp` `setNowLoginTimeGetLastLogoutTime`** — `if (!result.has_value())`
   then calls `result.value()` (which throws on empty optional); condition is
   inverted — the error branch reports on success and silently passes on
   failure.
5. **`envvars.cpp` `EnvVars::set`** — passes `envVar.c_str()` of a **local**
   `std::string` to `putenv`, which keeps the pointer → dangling/UB after
   return. Fix: `setenv` (POSIX) or keep the string alive.
6. **`memcache.hpp` move constructor** — moves the members but the cleaner
   thread keeps calling `cleanExpiredEntries` on the *moved-from* object
   (`this` was captured at thread creation); use-after-move. Also the moved
   thread is later `join()`ed by the destination's destructor while operating
   on foreign state. Fix: stop+join before moving, restart thread afterwards.
7. **`dosdetector.cpp` `processRequest`** — a *rate limit* fires when the IP
   has more distinct fingerprints than `MAX_FPS`, and a *ban* when one
   fingerprint repeats more than `MAX_REQUESTS` times. The README describes
   `MAX_REQUESTS` as "max requests per period" — the actual mapping is
   fingerprints↔rate-limit, repeat-count↔ban. Tests assert the implemented
   behaviour and flag the doc mismatch.
8. **`threadsafequeue.hpp`** — `pop()` never blocks although a
   `condition_variable` is maintained and `push` notifies it; consumers must
   poll. Documented (not a bug per se, dead code + misleading name).
9. **`envloader.cpp` `parseSet`** — `"a,,b"` yields an *empty-string* entry in
   the whitelist/blacklist set; an empty regex pattern matches everything, so
   this would whitelist/blacklist every IP.
10. **`envloader.hpp` `getEnvironmentVariable<T>`** — `std::stoi`/`std::stoull`
    throw on non-numeric input and are not caught; a malformed env var aborts
    the whole config (and hence server startup) instead of falling back to the
    default. The time variant `stoull` also silently accepts negative numbers
    (wraps to ~2^64).

## Gaps (would need a bigger refactor — see final report)

- ~~Injected clock into `MemCache` (TTL expiry currently needs real time).~~ **Closed (2026-11-13):** `MemCache<T, ClockT>` template seam added; `ManualClock` in `tests/support/fake_clock.hpp`; 4 TTL tests added.
- ~~`KeeprBase::validateToken` needs DB seam.~~ **Closed (2026-11-13):** 5 tests in `tests/gatekeeper/test_keeprbase_validatetoken.cpp` using `FakeDatabaseController`.
- ~~`SessionManager::login` needs DB seam.~~ **Closed (2026-11-13):** 3 login tests added to `test_sessionmanager.cpp`.
- ~~`Store` untested.~~ **Closed (2026-11-13):** 5 tests in `tests/utils/test_store.cpp`; also exposes a new TOCTOU race (bug 11).
- Drogon integration tests (real HTTP round trips) behind `[.integration]`. *(skeleton in `tests/controllers/test_integration_db.cpp` — full implementation requires a provisioned DB)*

### Remaining permanent gaps

- `src/server/` bootstrap.
- `src/database/` pool + query execution (real PostgreSQL only).
- Drogon filter success paths (`fccb`).
- `Communicate` (libcurl, no network-free surface).
- `Logger` construction test only: `logger.cpp` hardcodes the log directory; injectable path would require a 2-line production change (documented in `TEST_COVERAGE_PLAN.md`).

## Known production bugs / suspicious behaviour (updated 2026-11-13)

Bugs 1–10 are unchanged. A new bug was found:

11. **`store.hpp` `Store::getObject` TOCTOU race** — Two threads can both pass `inventory.find(typeid(T)) == inventory.end()` before either inserts the object, so both call `registerObject` and create two distinct objects. The second silently overwrites the first. Fix: hold `inventoryMutex` across the check-and-insert in `getObject` (merge the two lock scopes into one). Documented in `tests/utils/test_store.cpp` `[.known-bug][!shouldfail]`.
