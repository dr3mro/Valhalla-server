# Valhalla Server: Performance Improvement Plan

Findings from a read-through of the request path (Drogon handler -> GateKeeper -> Controller -> DatabaseController -> Database/libpqxx). Steps are ordered: measure first, then cheap and low-risk wins, then structural changes. Re-measure after every step and keep a change only if the numbers improve.

Key files: `src/controllers/base/controller/controller.hpp`, `src/controllers/databasecontroller/databasecontroller.{hpp,cpp}`, `src/database/{database,databaseconnectionpool}.cpp`, `src/gatekeeper/**`, `src/server/server.cpp`, `src/configurator/defaults.hpp`, `CMakeLists.txt`.

Completed: Steps 0-4 (all queries bound, detector archived in `archive/sqlinjectiondetector/`, sqlmap clean) (results in `docs/perf/results.md`, history in git).

## Step 5: Connection pool behavior

Done in code (not yet built or measured): `BlockingPool<T>` replaces the pool's own queue, `DB_ACQUIRE_TIMEOUT_MS` (default 3000) sets the wait, a timeout throws `DatabaseBusy` which becomes 503 with `Retry-After: 1` (controllers, login, Auth filter), `InUseGuard` is gone, `DatabaseHanndler` cannot be copied, the watchdog no longer spins when the pool is busy, and startup warns when `DB_MAX_CONN < SERVER_THREADS + 1`. Also fixed: `reconnect_all` used to wait for reconnects after releasing the pool lock; the three controller catch blocks that never answered now reply.

Left to do:

1. Build, run the unit tests (new: `test_blockingpool.cpp`, `test_database_busy.cpp`, busy-login test, config default).
2. Run the API tests and `sqlmap_test.sh`; check that a 503 under an undersized pool (for example `DB_MAX_CONN=2` with `SERVER_THREADS=10`) has the JSON body and `Retry-After`.
3. Size the pool: with `perf_baseline.sh --reset-db` at 128 VUs try `DB_MAX_CONN` = threads + 1, 2x and 4x threads; keep the smallest size that stops adding throughput and stay below Postgres `max_connections`. Add the run as the next stage column. Mention PgBouncer in the docs for several instances.

Done when: a run at the highest concurrency with the recommended pool size shows no connection-timeout errors, only queueing; an undersized pool gives 503 with `Retry-After`, never a generic 500.

## Step 6: Take scrypt off the event loop

Done in code (not yet built or measured): login runs its database reads on the request thread, then submits "verify password, write `last_login`, issue token, answer" to a bounded `PasswordWorker` (`PASSWORD_WORKERS` default 2, `PASSWORD_QUEUE_MAX` default 64; 0 workers = inline, which the unit tests use). A wrong password no longer updates the session row. Client create and update (which hash the password) run on the same worker. A full queue answers 503 with `Retry-After`. `executeControllerMethod` now captures the response callback by value, because answers can come from another thread after the handler returned. The k6 script `tools/k6/login_burst.js` runs reads alone, then reads during a login burst, and `perf_baseline.sh` reports both.

Left to do:

1. Build and run the unit tests (new: `test_passwordworker.cpp`, session-manager split tests, wrong-password-writes-nothing tests, config check).
2. Run `tools/perf_baseline.sh --reset-db` and compare "reads alone" with "reads during login burst" in the new report section; read p99 should stay close. Check the server log for 503s during the burst; if there are many, raise `PASSWORD_WORKERS`/`PASSWORD_QUEUE_MAX` (keep workers below `DB_MAX_CONN`).
3. Run the login/logout API tests and `sqlmap_test.sh`.
4. Done in code: new passwords are hashed with Argon2id at the OWASP minimum (19 MiB, 2 passes) instead of scrypt at the test-only minimum. `verifyPassword` reads the algorithm from the stored string, so existing scrypt hashes still log in. Run the burst test with this in place (the benchmark seeds new users, so they get Argon2id) and size `PASSWORD_WORKERS` from the measured cost per login.
5. The global rate limit (`MAX_REQUESTS` per `PERIOD`) is the only brake on password guessing; consider a stricter limit for the login route.

Done when: p99 of a read endpoint stays flat while a burst of logins runs, and a failed login leaves the session row untouched.

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
