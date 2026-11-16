# Async database plan (1.0.3)

Goal: make database access asynchronous so a Drogon IO thread never blocks on PostgreSQL. No worker pool, no thread hand-off, and reads stay fast even when queries are slow.

Status legend: `[ ]` to do, `[x]` done. Items marked **(manual)** are done by the maintainer, not by the coding agent.

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
- [ ] **(manual)** Record the control numbers: native run with `DB_WORKERS=0`, `SERVER_THREADS=10`, `DB_MAX_CONN=12`, `PASSWORD_WORKERS=2`.
- [ ] **(manual)** Add a stuck-query case with at least 12 simultaneous stuck requests (more than `SERVER_THREADS`) and record read p99 in inline mode. This is the case the async version must win.

## Phase 1: build and configuration

- [x] Enable `drogon/*:with_postgres=True` in `conanfile.txt` (keep `with_boost=False`) and confirm a clean build.
- [x] Add `DB_ASYNC` (default `false`) to `src/configurator/configurator.hpp` and `defaults.hpp`; document it in the `valhalla.sh` variable list.
- [ ] Create a `drogon::orm::DbClient` (PostgreSQL) from the existing `DB_*` settings, behind `DB_ASYNC`, sized from `DB_MAX_CONN`.
- [ ] Decide how the client is registered in `Store` and how it is replaced in tests (fake client).

## Phase 2: building blocks (no route changes yet)

- [ ] Adapter from `pqxx::params` to Drogon's incremental SQL binder, so the entity `getSql*Statement(pqxx::params&)` builders stay unchanged.
- [ ] Result-to-JSON mapper equivalent to the pqxx `field.type()` mapping in `Database::executeQuery` (`src/database/database.cpp`), same output including nulls and types.
- [ ] Error mapping: pool exhausted / timeout -> HTTP 503 (as `DatabaseBusy` does today); SQL errors -> the existing error responses.
- [ ] Unit tests for the adapter, the mapper and the error mapping.

## Phase 3: prototype on one route

Route: `POST /clients/providers/read`, only when `DB_ASYNC=true`.

- [ ] Async `DatabaseController` entry point returning a coroutine (`drogon::Task<...>`), next to the sync one.
- [ ] Permission check (`PermissionManager` / `PermissionCache` miss path) as a coroutine.
- [ ] The read itself as a coroutine; send the response from the IO loop with no hand-off.
- [ ] Keep `PasswordWorker` for hashing and make sure its completion is posted back to the IO loop.
- [ ] `api_tests` payloads give byte-identical bodies and status codes for sync and async.
- [ ] **(manual)** Benchmark sweep: target is the `DB_WORKERS=0` level (about 19.6k req/s and 0.4 ms median at 8 VUs).
- [ ] **(manual)** Stuck-query case (12 or more stuck requests): read p99 stays within 2x of reads-alone.
- [ ] **(manual)** Go / no-go decision before Phase 4.

## Phase 4: gatekeeper and remaining routes

- [ ] Session and token validation (`SessionManager`, `GateKeeper::isAuthenticationValid`) and the auth filter (`src/api/v2/filters/auth.hpp`).
- [ ] Login flow: `fetchPasswordHash` async, `completeLogin` on `PasswordWorker`, result posted back to the IO loop.
- [ ] All read routes (users, providers, clinics, services, staff, appointments, search, visits, get-services).
- [ ] Write routes: create / update / delete / suspend / unsuspend, with `pqxx::work` transactions replaced by Drogon transactions.
- [ ] `getNextID` and other sequence calls.
- [ ] Synchronous DB calls inside entities: `checkItemExists` in `src/entities/base/client.hpp` (username check) and `src/entities/base/entity.hpp` (id check) run during validation/construction, so they need an async validation step or a cached/inlined alternative.
- [ ] Entity files (`src/entities/**`) are almost all pure SQL/params builders (`getSql*Statement`, `getPermissionsQueryFor*`); they need no async change, only to keep compiling against the new params type. The only real DB calls in entities are the two `checkItemExists` calls above.
- [ ] Gatekeeper DB code, the full list of real call sites: `keeprbase.cpp` (`getPasswordHashForUserName` x2, `setNowLoginTimeGetLastLogoutTime`, `executeQuery` x2, `executeReadQuery`, `doReadQuery`), `permissionmanager_private.hpp` (`getPermissionsOfEntity` -> `getPermissions`, used by about 19 `PermissionManager` methods in `permissionmanager.cpp`), `sessionmanager.cpp` (login / timestamps), and `PermissionCache` invalidation after writes.
- [ ] Controller layer: `controllers/base/controller/controller.hpp` (`cruds`, `Search`, `GetServices`, `GetVisits` call `executeQuery`, `executeReadQuery` and `executeSearchQuery`) is the single place all CRUD routes go through; convert it once.
- [ ] Schema loading at boot (`src/validator/databaseschema/databaseschema.cpp`: `getTableSchema`, `getAllTables`): decide whether it stays on a small synchronous pqxx connection (then libpqxx stays a dependency) or moves to the async client before the event loop starts.
- [ ] Graceful shutdown: close the `DbClient` cleanly (replaces the executor shutdown in `Server::run()`).
- [ ] Container image: the image runs its own Postgres and overrides `DB_HOST` and `DB_SSL`; confirm the `DbClient` honours the same overrides and `DB_ACQUIRE_TIMEOUT_MS` behaviour.

## Phase 5: cleanup

- [ ] Make `DB_ASYNC=true` the default; keep `false` available for one release.
- [ ] Remove the DB side of `BoundedExecutor` (`DbExecutor`), `DbProbe`, `WouldBlock`, `DB_WORKERS`, `DB_QUEUE_MAX`, `DB_IO_FASTPATH`. Keep `PasswordWorker`.
- [ ] Remove the sync `DatabaseController` / `Database` / `DatabaseConnectionPool` / `WatchDog` paths that are no longer used, and the libpqxx dependency if nothing else needs it.
- [ ] Update `README.md`, `PLAN.md`, `docs/` and `valhalla.sh` (variables, defaults, help text).

## Phase 6: verification and release

- [ ] Redesign the test seam: `tests/support/fake_db.hpp` currently fakes the sync `DatabaseController` virtuals; add a fake for the async client.
- [ ] Catch2 suite passes; `api_tests` pass; `schemathesis_test.sh`, `sqlmap_test.sh`, `fuzz_auth.sh`, `cors_test.sh` pass.
- [ ] Lint and format pass (`tools/lint_sh.sh`, clang-format, clang-tidy, cppcheck).
- [ ] **(manual)** Full sweep, mixed benchmark, login burst and stuck-query case, compared with 1.0.1, 1.0.2 and the `DB_WORKERS=0` control.
- [ ] **(manual)** Docker image build and run through `valhalla.sh`.
- [ ] **(manual)** Tag `1.0.3`.

## Risks

- Large refactor touching many files; keep it behind the flag until the numbers are confirmed.
- Coroutine and callback lifetimes (request, callback and captured state must outlive the await).
- Drogon manages connections differently from the current pool and watchdog (reconnects, timeouts, max connections).
- Transaction semantics for writes must match `pqxx::work`.
- The type mapping between PostgreSQL and JSON must not change any response.
- The Drogon PostgreSQL Conan option has not been built yet (verified in Phase 1).
