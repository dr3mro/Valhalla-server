# Valhalla Server: Performance Improvement Plan

Findings from a read-through of the request path (Drogon handler -> GateKeeper -> Controller -> DatabaseController -> Database/libpqxx). Steps are ordered: measure first, then cheap and low-risk wins, then structural changes. Re-measure after every step and keep a change only if the numbers improve.

Key files: `src/controllers/base/controller/controller.hpp`, `src/controllers/databasecontroller/databasecontroller.{hpp,cpp}`, `src/database/{database,databaseconnectionpool,inuseguard}.cpp`, `src/gatekeeper/**`, `src/server/server.cpp`, `src/configurator/defaults.hpp`, `CMakeLists.txt`.

## Step 0: Establish a baseline

Goal: know where the time goes before changing anything.

1. Run a Release build against a seeded Postgres (see `docker/`, `valhalla.sh`).
2. Load-test a representative mix with `wrk`, `hey` or `k6` using the existing `api_tests`/`Insomnia.yaml` requests: login, read one entity, create one entity, search.
3. Record per endpoint: requests/s, p50/p95/p99 latency, error rate (watch for 4xx/5xx and "Timeout while waiting for a connection" in logs).
4. Run each test at several concurrency levels (1, 8, 32, 128) to see where throughput stops scaling.
5. Profile one run with `perf record -g` (or `perf top`) and check `pg_stat_statements` on the database side.
6. Save results in `docs/perf/baseline.md` so later steps can be compared.

Done when: a table of baseline numbers is committed.

## Step 1: Zero-risk quick wins

1. **Build flags.** In `CMakeLists.txt` change Release from `-O2` to `-O3` and enable LTO (`CMAKE_INTERPROCEDURAL_OPTIMIZATION`). Keep `-DNDEBUG`.
2. **Log noise.** `Database::executeQuery` emits `LOG_WARN` for every null field in every row. Remove it or lower it to debug level.
3. **Avoid double JSON work.** Handlers convert results to `jsoncons::json` and then `.as<std::string>()` again. Remove copies (move instead of copy, e.g. `reply.push_back(std::move(object))`; clear/reuse `object` per row instead of leaking fields between rows).
4. **Type switch fix.** The row conversion only knows TEXT/INTEGER/BOOLEAN/JSON/JSONB and falls back to strings, so bigint/numeric/timestamp come out as strings. Add the missing OIDs so clients do not need to re-parse.

Done when: baseline re-run shows no regression and a small gain.

## Step 2: Fix the Create path (fewer round trips, no wasted work)

1. In `EntityController<T>::Create` (`src/controllers/entitycontroller/entitycontroller.cpp`) `getNextID` runs before the body is parsed or validated. Move it after validation and permission checks so rejected requests do not take a connection or burn a sequence value.
2. Replace the separate `SELECT NEXTVAL(...)` query with `INSERT ... RETURNING id` (or `DEFAULT nextval(...)` in the column), so a create is one query instead of two.
3. Apply the same idea to Update/Delete where the code does a read before the write.

Done when: a create makes a single DB round trip (check with `log_statement=all` in a test DB).

## Step 3: Cache permission lookups

`PermissionManager` runs a DB query on every authorized request (`getPermissionsOfEntity`). There is already an open TODO in `permissionmanager.cpp` for this.

1. Add a TTL cache (the project already has `utils/memcache`) keyed by `(entity type, entity id)` holding the permissions JSON.
2. Invalidate the key on every update/delete of the entity, on staff add/remove, and on suspend/unsuspend.
3. Keep the TTL short (e.g. 30-60 s) as a safety net, and make it configurable.
4. Add unit tests for hit, miss, invalidation and expiry (`tests/gatekeeper/`).
5. Be explicit about the security trade-off: a revoked permission can stay valid until invalidation/TTL, so invalidation must be wired into every write path.

Done when: a repeated read of the same entity does not issue a permission query.

## Step 4: Cheaper SQL-injection scanning

Every query is scanned with about 35 `std::regex` patterns, a keyword scan and quote/semicolon checks. This is done for the plain query and again inside `executeQuery`. `std::regex` is slow, and the executed statement is already parameterized.

1. Compile the regexes once (already static) and stop scanning the same text twice; scan only the plain query that contains the user values.
2. Do cheap checks first (the `;`, `/*`, `--` and keyword checks) and run the regexes only when something suspicious is found.
3. Longer term, replace `std::regex` with RE2 or a small hand-written scanner.
4. Do not remove detection without sign-off: the existing tests (`tests/gatekeeper/test_sqlinjectiondetector.cpp`, `sqlmap_test.sh`, `schemathesis_test.sh`) must still pass unchanged.

Done when: profile shows the detector is no longer a top-5 symbol and all injection tests pass.

## Step 5: Connection pool behavior

1. `DatabaseConnectionPool::get_connection()` waits at most 1 s and then returns null, which surfaces as failed requests under load. Make the wait configurable and return a clear 503 when it is exceeded.
2. `InUseGuard` adds a second per-connection mutex/condition variable with its own 1 s timeout, even though the pool already hands a connection to one caller at a time. Remove it, or keep it only for the watchdog/reconnect path.
3. Size the pool from measurements: start with `DB_MAX_CONN` about 2x server threads, and stay below Postgres `max_connections`. Consider PgBouncer if several instances run.
4. Make the pool return connections reliably (RAII handle) so an exception cannot leak one.

Done when: a load test at the highest concurrency produces no connection-timeout errors, only queueing.

## Step 6: Take scrypt off the event loop

`PasswordCrypt::verifyPassword` (libsodium scrypt) is intentionally slow and runs on a Drogon IO thread, so a handful of simultaneous logins can freeze every other request on those threads.

1. Run password verification on a dedicated worker pool (or `drogon::app().getLoop()` / a `trantor::ConcurrentTaskQueue`) and complete the callback from there.
2. Cap concurrent logins, and keep or tighten the existing rate limiting on the login route.
3. Do not weaken the scrypt parameters to gain speed.

Done when: p99 of a read endpoint stays flat while a burst of logins runs.

## Step 7: Stop blocking IO threads on the database (largest change)

Handlers call libpqxx synchronously, so each of the `SERVER_THREADS` (default 4) threads is blocked for the full duration of every DB call, and a slow query stalls all unrelated requests that share the thread.

Pick one approach:

- **A (smaller change):** keep libpqxx but run all DB work on a separate worker pool sized to the connection pool; the handler returns immediately and the callback is invoked from the worker. The controller APIs already take callbacks, so this fits the current design.
- **B (larger change):** switch to Drogon's async DB client (`drogon::orm::DbClient`) with a connection count matching `DB_MAX_CONN`, which gives non-blocking queries and built-in pipelining.

Steps for A:
1. Introduce a `DbExecutor` (thread pool plus bounded queue) in `DatabaseController::executer`.
2. Make the queue bounded and return 503 when full, so overload degrades gracefully.
3. Keep the permission check, query and response in one task to avoid extra thread hops.
4. Raise `SERVER_THREADS` only if profiling shows IO threads are busy with non-DB work.

Done when: throughput scales with DB pool size instead of server thread count, and slow queries no longer raise latency of unrelated endpoints.

## Step 8: Query-level tuning

1. Use prepared statements (`pqxx::connection::prepare`) for the hot, fixed queries (login lookup, permission lookups, CRUD by id) so Postgres does not re-plan them each time.
2. Check `EXPLAIN ANALYZE` for the search and permission queries and add the missing indexes (foreign keys, username, group/owner ids). Put new indexes in `init.sql` and the schema migration path.
3. Search: avoid `OFFSET` for deep pages (keyset pagination) and keep the `limit + 1` trick.
4. Return only the needed columns in list/search responses.

Done when: `pg_stat_statements` shows the top queries with low mean time and no sequential scans on large tables.

## Step 9: Verify, guard and document

1. Re-run the Step 0 suite and compare with the baseline in `docs/perf/`.
2. Run the full test suite, `api_tests`, `sqlmap_test.sh`, `schemathesis_test.sh` and `fuzz_auth.sh` to confirm no functional or security regression.
3. Add a small load-test smoke job (low thresholds) to CI so large regressions are caught.
4. Record the final numbers and the chosen defaults (`SERVER_THREADS`, `DB_MAX_CONN`, cache TTLs) in the docs.

## Suggested order and risk

| Step | Effort | Risk | Expected gain |
|------|--------|------|---------------|
| 0 Baseline | Low | None | Enables everything else |
| 1 Quick wins | Low | Low | Small |
| 2 Create path | Low | Low | Medium (writes) |
| 3 Permission cache | Medium | Medium (stale permissions) | High (reads) |
| 4 Injection scan | Medium | Medium (security) | Medium |
| 5 Pool behavior | Low | Low | Medium under load |
| 6 Scrypt off loop | Medium | Low | High for login bursts |
| 7 Async DB | High | Medium | Highest |
| 8 Query tuning | Medium | Low | Depends on data size |
| 9 Verify | Low | None | Confidence |
