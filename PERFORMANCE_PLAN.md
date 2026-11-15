# Valhalla Server: Performance Improvement Plan

Findings from a read-through of the request path (Drogon handler -> GateKeeper -> Controller -> DatabaseController -> Database/libpqxx). Steps are ordered: measure first, then cheap and low-risk wins, then structural changes. Re-measure after every step and keep a change only if the numbers improve.

Key files: `src/controllers/base/controller/controller.hpp`, `src/controllers/databasecontroller/databasecontroller.{hpp,cpp}`, `src/database/{database,databaseconnectionpool}.cpp`, `src/gatekeeper/**`, `src/server/server.cpp`, `src/configurator/defaults.hpp`, `CMakeLists.txt`.

Completed: Steps 0-4 (all queries bound, detector archived in `archive/sqlinjectiondetector/`, sqlmap clean) (results in `docs/perf/results.md`, history in git).

## Step 5: Connection pool behavior

Left to do:

1. Build, run the unit tests (new: `test_blockingpool.cpp`, `test_database_busy.cpp`, busy-login test, config default).
2. Run the API tests and `sqlmap_test.sh`; check that a 503 under an undersized pool (for example `DB_MAX_CONN=2` with `SERVER_THREADS=10`) has the JSON body and `Retry-After`.
3. Size the pool: with `perf_baseline.sh --reset-db` at 128 VUs try `DB_MAX_CONN` = threads + 1, 2x and 4x threads; keep the smallest size that stops adding throughput and stay below Postgres `max_connections`. Add the run as the next stage column. Mention PgBouncer in the docs for several instances.

Done when: a run at the highest concurrency with the recommended pool size shows no connection-timeout errors, only queueing; an undersized pool gives 503 with `Retry-After`, never a generic 500.

## Step 6: Take scrypt off the event loop

Left to do:

1. Build and run the unit tests (new: `test_passwordworker.cpp`, session-manager split tests, wrong-password-writes-nothing tests, config check).
2. Run `tools/perf_baseline.sh --reset-db` and compare "reads alone" with "reads during login burst" in the new report section; read p99 should stay close. Check the server log for 503s during the burst; if there are many, raise `PASSWORD_WORKERS`/`PASSWORD_QUEUE_MAX` (keep workers below `DB_MAX_CONN`).
3. Run the login/logout API tests and `sqlmap_test.sh`.
4. The global rate limit (`MAX_REQUESTS` per `PERIOD`) is the only brake on password guessing; consider a stricter limit for the login route.

Done when: p99 of a read endpoint stays flat while a burst of logins runs, and a failed login leaves the session row untouched.

## Step 7: Stop blocking IO threads on the database (largest change)

Implemented with approach A (libpqxx on a bounded `DbExecutor`; the Drogon async client was rejected as too large a rewrite). Left to do:

1. Build and run the unit tests (new: `tests/concurrency/test_boundedexecutor.cpp`, `DbExecutorConfig` check); run the API tests and `sqlmap_test.sh`.
2. Add a slow-query scenario to `tools/perf_baseline.sh` (a few VUs on a `pg_sleep`-style endpoint while others read) and check that read p99 stays flat.
3. Run `tools/perf_baseline.sh` as Stage 6 and compare with Stage 5; the mixed-run drop seen in Stage 5 (14.2k to 10.2k req/s) should be explained or recovered.
4. Tune `DB_WORKERS` (default `DB_MAX_CONN`) and `DB_QUEUE_MAX` (default 256).
5. Check shutdown order: `DbExecutor` must drain and join before the connection pool is destroyed.
6. Check that replying from a worker thread works with the `ElapsedTime` middleware and the Drogon callbacks under load.
7. Add a handler-level test for the queue-full 503.

Done when: throughput scales with `DB_WORKERS` and the pool size instead of `SERVER_THREADS`, a slow query no longer raises the latency of unrelated endpoints, overload returns 503 quickly, and the Stage 6 numbers are recorded.

## Step 8: Query-level tuning

Do Step 7's verification and the Stage 6 run first, so a Step 8 change cannot be confused with a Step 7 regression.

1. Measure first: enable `pg_stat_statements`, seed far more rows than the benchmark does (indexes and sequential scans do not show on small tables), run `tools/perf_baseline.sh`, and list the top queries by total time and mean time. Items 2-5 only touch queries that appear there.
2. Use prepared statements (`pqxx::connection::prepare`) for the hot, fixed queries (login lookup, permission lookups, CRUD by id) so Postgres does not re-plan them each time. Dynamic search queries (variable filters and ordering) stay unprepared.
3. Check `EXPLAIN ANALYZE` for the search and permission queries and add the missing indexes (foreign keys, username, group/owner ids). Put new indexes in `init.sql` and the schema migration path.
4. Search: avoid `OFFSET` for deep pages (keyset pagination) and keep the `limit + 1` trick.
5. Return only the needed columns in list/search responses.

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
