# Valhalla Server: Performance Plan

Steps 0-6 are done (results in `docs/perf/results.md`, history in git). Mixed throughput went from 4.0k req/s (baseline) to 14.2k (Stage 4). Steps 5-7 are implemented but only partly verified: Stage 5 gave 10.2k and Stage 6 (database work on the `DbExecutor`) gave 9.1k, so the last two steps cost throughput and nobody has explained why yet. This plan was rewritten from a read of the current source; every item names the file it comes from.

Rules: profile or measure first, change one thing, re-run `tools/perf_baseline.sh`, keep the change only if the numbers improve (or, for security fixes, do not get worse).

Key files: `src/api/v2/{filters,middlewares,basic,helper}`, `src/gatekeeper/**`, `src/controllers/**`, `src/database/**`, `src/concurrency/`, `src/server/server.cpp`.

## Step 7: Settle the DB executor

The pool is used (startup prints `DB workers: 10`), so the Stage 6 slowdown is real. The code side is done and needs building and running: `tools/perf_baseline.sh` now also runs a slow-query scenario (a background `psql` locks the `users` table while 4 clients read it and 8 clients read `providers`; needs `psql`), `BoundedExecutor` only wakes a worker when one is idle and counts tasks, rejections, queue depth and queue wait, and both executors are stopped in `Server::run` before the pool goes away. Left to do:

1. **Control runs** with the new script, same machine, each twice: `DB_WORKERS=0 tools/perf_baseline.sh --label step7-inline`, then `DB_WORKERS=2`, `4`, `10`, `20` (`--label step7-w4` and so on). Compare the mixed numbers and the "Slow query" table: with workers, read p99 must stay close to "reads alone"; inline it should follow the stuck requests. Stage 5 vs Stage 6 differences of 5-10% are inside run-to-run noise on this machine.
2. **Run each worker count with `DB_IO_FASTPATH` false and true.** Read the executor line the server prints when it stops (`DB executor: N tasks, M rejected, deepest queue D, average wait W us`). An average wait of tens of microseconds means the hop is cheap; hundreds or a deep queue means too few workers or lock contention.
3. **The hop is now cheaper** (code done, needs the control runs): each worker has its own queue and lock, `submit()` goes round-robin to an idle worker, wakes only that one, and a worker with nothing to do takes tasks from the others before it sleeps. `DB_IO_FASTPATH=true` also tries every request on the IO thread first: requests that are answered from memory (bad body, failed validation, permission refused from the cache) finish there, and the first call that needs a connection moves the request to a worker, which runs it again from the start. Most requests do need the database, so this repeats the parsing and validation for them; keep it off unless a run with it on is faster, and run it also with a mix of bad requests.
4. Pick the default `DB_WORKERS` and `DB_QUEUE_MAX` from step 1 and record them.

Done when: a slow query no longer raises the latency of unrelated endpoints, throughput is back at or above the Stage 4 level, and overload returns 503 quickly.

## Step 8: Per-request cost on the IO threads (new)

Every request runs the rate-limit filter, the auth filter and the response advice on a Drogon IO thread, before any database work. Most of this was never measured. First take one 30 s profile (Instruments Time Profiler on macOS, or `perf` in the container) at 32 VUs of `providers/read`; the items below are listed by how likely they are to show up.

1. **Auth fast path never checks the token (security and speed).** `SessionManager::clientHasValidSession` decodes the JWT, checks `exp` and the IP claim, then looks the session up by `group_id`. It does not verify the signature and does not compare the token with the one stored at login, so anyone who can write a JWT with a logged-in client's id and group is let in. Fix: look the session up by a hash of the token (or compare the stored token in constant time) and take id, group and expiry from the stored session; then a session hit needs no JWT parse at all (`jwt::decode` plus picojson per request today). Signature verification stays in the slow path (`TokenManager::isTokenValid`). Add a unit test with a forged token. Also decide whether a session hit must re-check `is_active` (today a suspended client keeps its session unless the suspend code removes it).
2. **DoS detector (`rate-limit` filter):** per request it takes five global mutexes (whitelist, blacklist, rate-limited, banned, requests), looks `GateKeeper` up in the Store (`filters/ratelimit.hpp:25`, a method, not a member), copies every request header into a new `unordered_map<string,string>`, copies the IP, method and path, and formats the request fingerprint hash into a string used as a map key. The whitelist and blacklist are `const` sets but are still locked. Fix: store the `GateKeeper`/detector pointer as a member (the filter already has one); pass headers by reference or hash them in place; make the lists immutable and lock-free (compile the patterns once at startup); use the 64-bit fingerprint as the key; take one lock (or shard by IP) for the counters. Also: a rate-limited request calls `processRequest` a second time, and each fingerprint keeps a deque of up to `MAX_REQUESTS` timestamps per IP (memory grows with distinct fingerprints).
3. **Logging per request:** the default `SERVER_DEBUG_LEVEL` is 1 (info), so `ElapsedTime` builds a coloured `fmt` line and writes it for every request. Compare a run at level 2 (warning). If it matters, log only slow requests or only at debug level.
4. **Session and permission caches use one mutex each and write on every read:** `MemCache::get` (sessions) moves the entry to the front of a list under the lock, and `BasicPermissionCache::get` renews the idle timer and returns a deep copy of the JSON under the lock. With several IO threads and DB workers this serializes. Options: renew the timer only when more than half the TTL has passed; return a `shared_ptr<const json>`; shard the maps by key hash.
5. **Response path:** the response-creation advice adds five headers (`Access-Control-*`, `Allow`, `X-Powered-By`) to every response as new strings; error bodies go through `Json::Value::toStyledString()` (pretty-printed). Cache the CORS header strings and write compact JSON.
6. **Drogon settings are all defaults** (`server.cpp`): set and test the idle connection timeout, keep-alive request limit, maximum body size, and whether the `Server`/`Date` headers are needed. These affect memory and tail latency more than throughput.
7. Smaller: `Validator::clientRegexValidation` and the DoS patterns now compile once (done); `Entity` objects built per request are cheap now; keep an eye out for new `Store::getObject` calls inside methods (use `Store::cachedObject`).

Done when: the profile shows no filter or middleware function above about 3% of the time, and mixed throughput is above the Stage 4 level.

## Step 9: Database round trips and statements (new)

1. **Count round trips per endpoint** first (set `log_statement=all` for one run of `test_api.sh`). Expected targets: reads 1, writes 1-2.
2. **Writes use `pqxx::work`:** `executeQuery<..., pqxx::work>` sends `BEGIN`, the statement and `COMMIT` (three round trips). A single INSERT, UPDATE or DELETE is already atomic, so use `pqxx::nontransaction` for single-statement writes; keep `work` only for multi-statement operations (check `Database::executeQuery` callers).
3. **Existence checks before writes:** create paths call `checkItemExists` (one round trip) before the insert. Replace with `INSERT ... ON CONFLICT DO NOTHING RETURNING id` or rely on the unique constraint and map the error.
4. **Prepared statements** (`pqxx::connection::prepare`) for the hot fixed queries: login lookup, permission lookups, read/update/delete by id. Every connection must prepare them (also after `reconnect`). Dynamic search queries stay unprepared.
5. **Result conversion:** `Database::executeQuery` builds a `jsoncons::json` object per row (field names allocated as strings, JSONB columns parsed from text) and the controllers stringify it again. For large list/search results, write the JSON text straight from the `pqxx::result`; for JSONB columns, pass the text through without parsing.

Done when: the round-trip count per endpoint is recorded in `docs/perf/` and no endpoint does a `BEGIN`/`COMMIT` around a single statement.

## Step 10: Query tuning

Do Steps 7-9 first, so a query change cannot be confused with a request-path regression.

1. Measure: enable `pg_stat_statements`, seed far more rows than the benchmark does (indexes and sequential scans do not show on small tables), run `tools/perf_baseline.sh`, and list the top queries by total and mean time. Later items only touch queries that appear there.
2. `EXPLAIN ANALYZE` for the search and permission queries; add the missing indexes (foreign keys, username, group/owner ids) to `init.sql` and the schema migration path.
3. Search: keyset pagination instead of deep `OFFSET`; keep the `limit + 1` trick.
4. Return only the needed columns in list and search responses.

Done when: `pg_stat_statements` shows low mean times for the top queries and no sequential scans on large tables.

## Step 11: Verify, guard and document

1. Re-run the Step 0 suite and compare with the baseline in `docs/perf/`.
2. Run the full unit tests, `api_tests`, `sqlmap_test.sh`, `schemathesis_test.sh` and `fuzz_auth.sh`; the Step 8 auth change needs the login/logout API tests and a forged-token test.
3. Add a small load-test smoke job (low thresholds) to CI so large regressions are caught.
4. Record the final numbers and chosen defaults (`SERVER_THREADS`, `DB_MAX_CONN`, `DB_WORKERS`, `PASSWORD_WORKERS`, cache TTLs, log level) in the docs.

## Suggested order and risk

| Step | Effort | Risk | Expected gain |
|------|--------|------|---------------|
| 7 Settle the DB executor | Medium | Low | Explains the Stage 5-6 drop, protects against slow queries |
| 8 IO-thread cost | Medium | Medium (auth change needs care) | High: runs on every request, and fixes a security hole |
| 9 Round trips, statements | Medium | Medium | High for writes, medium for reads |
| 10 Query tuning | Medium | Low | Depends on data size |
| 11 Verify | Low | None | Confidence |
