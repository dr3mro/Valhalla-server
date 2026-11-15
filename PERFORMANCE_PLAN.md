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

Today `SessionManager::login` runs on a Drogon IO thread and does, in order: DB read of the hash, DB write of `last_login` (before the password is checked, so a failed login still updates it), then `PasswordCrypt::verifyPassword` (scrypt, blocking). Client create and update also call `hashPassword` on the IO thread (`types.hpp`). The scrypt cost is small now only because hashing uses `OPSLIMIT_MIN`/`MEMLIMIT_MIN`; the planned move to `*_INTERACTIVE` (Step 9) makes each verify about 100 ms and would freeze every request on that thread, so this step must land first. Stored hashes encode their own parameters, so raising them later needs no migration.

### 6.1 Fix the order in login

Verify the password first, then write `last_login`. A wrong password must not touch the session row. Add a test that fails today (FakeDb records whether the write ran).

### 6.2 A bounded password worker

1. New `PasswordWorker` (`src/gatekeeper/passwordcrypt/`): a small fixed pool (`trantor::ConcurrentTaskQueue` or own threads) with `submit(task)` that returns false when the queue is full.
2. Config through the Configurator, with `.env`, `valhalla.sh` help/README and docs: `PASSWORD_WORKERS` (default: min(2, cores/4), at least 1) and `PASSWORD_QUEUE_MAX` (default 64).
3. A test mode that runs tasks inline, so existing tests stay synchronous.
4. Keep workers fewer than the DB pool size, because the post-verify steps use the database.

### 6.3 Async login

1. `GateKeeper::login`: parse and read the hash on the IO thread as now, then submit "verify, set `last_login`, generate token, call the callback" to the worker. The Drogon callback may be called from any thread; capture everything by value (`shared_ptr`s for managers).
2. Queue full returns 503 with `Retry-After`, never blocks.
3. Same worker for `hashPassword` in client create and update (do the hash in the controller before building the entity, or submit the whole create).
4. Keep the existing login rate limit; confirm which limiter covers the login route and tighten it if the per-IP limit is loose.
5. Do not weaken the scrypt parameters. Optional 6.5: raise to `*_INTERACTIVE` once this is measured.

### 6.4 Tests and measure

1. Unit tests: worker runs tasks and stops cleanly (drains on shutdown), queue-full is rejected, login failure does not write `last_login`, 503 path.
2. New k6 scenario: a burst of logins while the read sweep runs. Record read p99 with and without the burst (stage 5 column; stage 4 is the last numbers recorded).
3. Run `sqlmap_test.sh` and the login/logout API tests again.

Risks: a callback fired after shutdown or on a destroyed object (capture by value, drain workers first); worker threads starving the DB pool; slower single login by one queue hop (negligible).

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
