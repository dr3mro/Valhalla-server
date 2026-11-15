# Valhalla Server: Performance Improvement Plan

Findings from a read-through of the request path (Drogon handler -> GateKeeper -> Controller -> DatabaseController -> Database/libpqxx). Steps are ordered: measure first, then cheap and low-risk wins, then structural changes. Re-measure after every step and keep a change only if the numbers improve.

Key files: `src/controllers/base/controller/controller.hpp`, `src/controllers/databasecontroller/databasecontroller.{hpp,cpp}`, `src/database/{database,databaseconnectionpool,inuseguard}.cpp`, `src/gatekeeper/**`, `src/server/server.cpp`, `src/configurator/defaults.hpp`, `CMakeLists.txt`.

Completed: Steps 0-4 (all queries bound, detector archived in `archive/sqlinjectiondetector/`, sqlmap clean) (results in `docs/perf/results.md`, history in git).

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
| 5 Pool behavior | Low | Low | Medium under load |
| 6 Scrypt off loop | Medium | Low | High for login bursts |
| 7 Async DB | High | Medium | Highest |
| 8 Query tuning | Medium | Low | Depends on data size |
| 9 Verify | Low | None | Confidence |
