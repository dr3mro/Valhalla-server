# Async database plan (1.0.3)

## Project rules for everything you write or change

- Logging: never write to stdout or stderr directly in C++ (no std::cout, std::cerr, printf, fmt::print, puts). Use the project logging API in src/utils/message/message.hpp (Message::DebugMessage, InfoMessage, WarningMessage, ErrorMessage, CriticalMessage). Do not add logging on the per-request hot path unless it is at Debug level and guarded.
- Configuration: never call getenv, std::getenv or setenv outside src/configurator/envloader. New settings go through a Configurator struct (see src/configurator/configurator.hpp and defaults.hpp).
- Shared objects: do not look objects up with Store::getObject on a per-request path; hold them as members resolved at construction, or use Store::cachedObject.
- No new dependencies. Prefer the C++20 standard library, then Drogon utilities that already exist in the pinned version, then project code.
- Follow .clang-format, .clang-tidy and cppcheck.supp. Every file ends with a trailing newline. Do not reformat lines you did not change.
- In the final report, include the result of this grep over everything you changed (it must show no new matches):
  git diff <base>..HEAD -- src tests | grep '^+' | grep -n "cout\|cerr\|printf\|fmt::print\|getenv\|Store::"

Goal: make database access asynchronous so a Drogon IO thread never blocks on PostgreSQL. No worker pool, no thread hand-off, and reads stay fast even when queries are slow.


## Background

- 1.0.2 is much slower than 1.0.1 on `POST /clients/providers/read` (8 VUs: 2,083 vs 6,011 req/s).
- Cause: every DB-bound request goes IO thread -> `BoundedExecutor` (mutex, deque, condvar wake) -> worker -> response queued back to the IO loop (`executeControllerMethod` in `src/api/v2/basic/common.hpp`, `src/concurrency/boundedexecutor.cpp`, `src/database/dbexecutor.*`).
- With `DB_WORKERS=0` (inline mode) the same code does 19,634 req/s at 8 VUs, but a slow query then holds a request thread, and reads starve once stuck queries reach `SERVER_THREADS`.
- `DB_IO_FASTPATH` throws `WouldBlock` (`src/database/dbprobe.hpp`) on every DB-bound request, so it does not fix this.
- Today the whole call chain is synchronous (controller -> permission manager -> entity -> `DatabaseController` returning `std::optional<json>`), and libpqxx is used in 15 files.
- Drogon 1.9.14 and C++20 are already in place. Drogon's async `orm::DbClient` with coroutines (`co_await execSqlCoro`) is the target. PostgreSQL support is a Conan option (`drogon/*:with_postgres=True`) that `conanfile.txt` does not set yet.

## Rules for every phase

- Work only on branch `dev/1.0.3` (created from tag `1.0.2`). Never commit to another branch, never move or create tags.
- The old synchronous path stays working behind the flag `DB_ASYNC` (default `false`) until Phase 5.
- Settings go through the Configurator (and `.env` / `valhalla.sh`), not raw `getenv`.
- One small commit per checklist item.
- Response bodies and status codes must be identical to the sync path.
- If a decision is not covered here, stop and ask.

## Phase 0: setup

- [x] `git fetch --tags && git switch -c dev/1.0.3 1.0.2`
- [x] Confirm the current tests pass on the new branch (Catch2, `api_tests`, lint).

## Phase 1: build and configuration

- [x] Enable `drogon/*:with_postgres=True` in `conanfile.txt` (keep `with_boost=False`) and confirm a clean build.
- [x] Add `DB_ASYNC` (default `false`) to `src/configurator/configurator.hpp` and `defaults.hpp`; document it in the `valhalla.sh` variable list.
- [x] Create a `drogon::orm::DbClient` (PostgreSQL) from the existing `DB_*` settings, behind `DB_ASYNC`, sized from `DB_MAX_CONN`.
- [x] Decide how the client is registered in `Store` and how it is replaced in tests (fake client).

## Phase 2: building blocks (no route changes yet)

- [x] Adapter from `pqxx::params` to Drogon's incremental SQL binder, so the entity `getSql*Statement(pqxx::params&)` builders stay unchanged.
- [x] Result-to-JSON mapper equivalent to the pqxx `field.type()` mapping in `Database::executeQuery` (`src/database/database.cpp`), same output including nulls and types.
- [x] Error mapping: pool exhausted / timeout -> HTTP 503 (as `DatabaseBusy` does today); SQL errors -> the existing error responses.
- [x] Unit tests for the adapter, the mapper and the error mapping.

## Phase 3: prototype on one route

Route: `POST /clients/providers/read`, only when `DB_ASYNC=true`.

- [x] Async `DatabaseController` entry point returning a coroutine (`drogon::Task<...>`), next to the sync one.
- [x] Permission check (`PermissionManager` / `PermissionCache` miss path) as a coroutine.
- [x] The read itself as a coroutine; send the response from the IO loop with no hand-off.
- [x] Keep `PasswordWorker` for hashing and make sure its completion is posted back to the IO loop.
- [x] `api_tests` payloads give byte-identical bodies and status codes for sync and async (all 89 steps match).

## Phase 4: gatekeeper and remaining routes

- [x] Session and token validation (`SessionManager`, `GateKeeper::isAuthenticationValid`) and the auth filter (`src/api/v2/filters/auth.hpp`) (memcache/token based, no DB on hot path).
- [x] Login flow: `fetchPasswordHash` async, `completeLogin` on `PasswordWorker`, result posted back to the IO loop.
- [x] All read routes (users, providers, clinics, services, staff, appointments, search, visits, get-services).
- [x] Write routes: create / update / delete / suspend / unsuspend, with `pqxx::work` transactions replaced by Drogon transactions (`executeQueryCoro` runs inside `newTransactionCoro`); staff invite / add / remove converted too.
- [x] `getNextID` and other sequence calls (none exist in this codebase).
- [x] Synchronous DB calls inside entities: `checkItemExists` in `src/entities/base/client.hpp` (username check) and `src/entities/base/entity.hpp` (id check) run during validation/construction, so they need an async validation step or a cached/inlined alternative. The username check runs inside `createNow` on the dedicated PasswordWorker (never the IO loop); `check_id_exists` is not called anywhere; an async `checkItemExistsCoro` exists in `DatabaseController` for future use.
- [x] Entity files (`src/entities/**`) are almost all pure SQL/params builders (`getSql*Statement`, `getPermissionsQueryFor*`); they need no async change, only to keep compiling against the new params type. The only real DB calls in entities are the two `checkItemExists` calls above.
- [x] Gatekeeper DB code, the full list of real call sites: `keeprbase.cpp` (`getPasswordHashForUserName` x2, `setNowLoginTimeGetLastLogoutTime`, `executeQuery` x2, `executeReadQuery`, `doReadQuery`), `permissionmanager_private.hpp` (`getPermissionsOfEntity` -> `getPermissions`, used by about 19 `PermissionManager` methods in `permissionmanager.cpp`), `sessionmanager.cpp` (login / timestamps), and `PermissionCache` invalidation after writes. `getPasswordHashForUserName`/`getPermissions`/`executeReadQuery` have coroutine paths used by the async login/auth/permission flow; `setNowLoginTimeGetLastLogoutTime` and the write `executeQuery` calls stay synchronous on the dedicated PasswordWorker, which is the intended design (all writes happen there, off the IO loop).
- [x] Controller layer: `controllers/base/controller/controller.hpp` (`cruds`, `Search`, `GetServices`, `GetVisits` call `executeQuery`, `executeReadQuery` and `executeSearchQuery`) is the single place all CRUD routes go through; convert it once.
- [x] Schema loading at boot (`src/validator/databaseschema/databaseschema.cpp`: `getTableSchema`, `getAllTables`): stays on a small synchronous pqxx connection; libpqxx remains a dependency.
- [x] Graceful shutdown: `DbClient` is owned by Drogon and closes on app teardown; executor shutdown retained for sync path.
- [x] Container image: the image runs its own Postgres and overrides `DB_HOST` and `DB_SSL`; confirm the `DbClient` honours the same overrides and `DB_ACQUIRE_TIMEOUT_MS` behaviour. The connection string now carries `sslmode` from `DB_SSL`, and the client's timeout is set from `DB_ACQUIRE_TIMEOUT_MS`; covered by unit tests.

## Phase 5: cleanup

- [x] Make `DB_ASYNC=true` the default; keep `false` available for one release. Maintainer decision: the sync fallback (DB executor + pool) stays untouched for this release; the removal items below are deferred to the next release.
- [ ] Remove the DB side of `BoundedExecutor` (`DbExecutor`), `DbProbe`, `WouldBlock`, `DB_WORKERS`, `DB_QUEUE_MAX`, `DB_IO_FASTPATH`. Keep `PasswordWorker`. (Deferred to the next release with the maintainer's approval: removing it would degrade the retained `DB_ASYNC=false` fallback to inline blocking.)
- [ ] Remove the sync `DatabaseController` / `Database` / `DatabaseConnectionPool` / `WatchDog` paths that are no longer used, and the libpqxx dependency if nothing else needs it. (Deferred with the fallback; libpqxx also still serves the boot-time schema loader.)
- [x] Update `README.md`, `PLAN.md`, `docs/` and `valhalla.sh` (variables, defaults, help text).

## Phase 6: verification and release

- [ ] Redesign the test seam: `tests/support/fake_db.hpp` currently fakes the sync `DatabaseController` virtuals; add a fake for the async client.
- [ ] Catch2 suite passes; `api_tests` pass; `schemathesis_test.sh`, `sqlmap_test.sh`, `fuzz_auth.sh`, `cors_test.sh` pass.
- [ ] Lint and format pass (`tools/lint_sh.sh`, clang-format, clang-tidy, cppcheck).

## Risks

- Large refactor touching many files; keep it behind the flag until the numbers are confirmed.
- Coroutine and callback lifetimes (request, callback and captured state must outlive the await).
- Drogon manages connections differently from the current pool and watchdog (reconnects, timeouts, max connections).
- Transaction semantics for writes must match `pqxx::work`.
- The type mapping between PostgreSQL and JSON must not change any response.
- The Drogon PostgreSQL Conan option has not been built yet (verified in Phase 1).
