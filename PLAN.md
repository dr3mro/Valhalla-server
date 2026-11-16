# Valhalla Server: Combined Plan (remaining work + performance)

## 0. CORS: top priority (front end is blocked; React at another port calls the API on `localhost:8080`)

Root cause: CORS headers were written in three places (creation advice, OPTIONS branch, 405 branch) and `Allow` in two, and `Access-Control-Allow-Methods` was never sent, so PUT/DELETE preflights fail.

- [x] One writer: `Cors::apply(req, resp)` in `src/server/cors.hpp`, called only from `registerPreSendingAdvice` in `server.cpp`. It removes each header before setting it, so applying it twice cannot duplicate anything. The creation advice and the `addCorsHeaders` calls are gone.
- [x] Headers: `Allow-Origin: *`, `Allow-Methods: GET, POST, PUT, DELETE, HEAD, OPTIONS`, `Expose-Headers: Retry-After, Content-Length`, `Allow-Headers` default `Content-Type, Authorization, Accept, X-Requested-With` (on a preflight, echo `Access-Control-Request-Headers`), `Max-Age: 7200` on preflight only. `Allow` now appears only on the 405 reply for Drogon-recognized methods; parse-level TRACE 405s (80 ST204 failures) still lack `Allow` and CORS headers because Drogon rejects them before the pre-routing advice. `X-Powered-By` was removed.
- [x] **Build and run (verified):** `HttpResponse::removeHeader`, `headers()`, `HttpRequest::getHeader` and `registerPreSendingAdvice(const HttpRequestPtr&, const HttpResponsePtr&)` all exist in Drogon 1.9.14 (conan.lock); the project builds (Debug + Release) and all 353 unit tests pass, including `tests/api/test_cors.cpp` (23 assertions, 6 cases).
- [x] Run `./cors_test.sh` against a running server: each CORS header appears exactly once for GET, 404, 401, PUT/DELETE preflight and 405 (all checks print "ok"). `tests/api/test_cors.cpp` passes. `test_api.sh` also passes (89 passed, 0 failed). No response carries `Allow` except 405 replies; `X-Powered-By` is gone.
- [x] Checked whether pre-sending advice covers parse-error 400s generated inside Drogon: it does **not**. A malformed request line (e.g. `GET /bad http/1.1` or `GARBAGE`) makes Drogon return `HTTP/1.1 400 Bad Request` with only `Connection: close` and no CORS headers, because the response is produced during parsing before the pre-sending advice runs. Not worked around, per task rules; front ends hitting such errors will still see a CORS failure. Recorded in README.md "Known limitation".
- [x] Follow-up task 1 (Vary): `Cors::apply` now appends to an existing `Vary` header instead of clobbering it; unit tests cover preserve/append and no-duplicate (21 assertions total).
- [x] Follow-up task 2 (wire script): `cors_test.sh` is ordered (non-provoking, 403, 503, 429), supports `--strict`/`STRICT=1`, verifies status codes, prints a summary and a trailing warning. Raw 429: `HTTP/1.1 429 Too Many Requests` with each CORS header once. Raw 503: `HTTP/1.1 503 Service Unavailable` with each CORS header once. 403 was provoked separately with `WHITELIST="" BLACKLIST="127.0.0.1"`: `HTTP/1.1 403 Forbidden` with each CORS header once.
- [x] Follow-up task 3 (Docker docs): README "Front-end development (CORS and Docker)" documents origin/methods/headers, bridge IP, anchored `WHITELIST`, and dev settings location. `tests/gatekeeper/test_dosdetector.cpp` covers the anchored pattern.
- [x] Follow-up task 4 (housekeeping): clang-format and clang-tidy run on changed files; cppcheck is not installed in this environment. Schemathasis re-run: ST204 still 80 failures (TRACE/other methods handled internally by Drogon without `Allow`); no new failure categories versus the previous run.
- [x] Follow-up task 5 (hot-path cost): 5 interleaved smoke runs each after removing the redundant `removeHeader` calls. main mixed throughput 6056/5729/5673/5153/4370 req/s (min 4370, max 6056), p99 0.30/0.31/0.35/0.37/0.45 ms. cors-fix 6555/6284/6013/5508/5054 req/s (min 5054, max 6555), p99 0.27/0.28/0.28/0.32/0.39 ms. The ranges overlap in both throughput and p99, so the remaining difference is run-to-run noise; no further change.
- [ ] Only if the front end needs cookies or `withCredentials`: add a `CORS_ALLOWED_ORIGINS` env setting (comma-separated, default `*`), echo the matching `Origin` with `Vary: Origin` and `Allow-Credentials: true`; never combine `*` with credentials.
- [ ] Docker: the front end's requests arrive from one gateway IP; the README now documents how to discover it (native Linux `docker network inspect bridge`, `docker exec ... ss`, `/proc/net/tcp`, or `docker logs`) and gives an anchored `WHITELIST` example. Docker was not available on the development machine, so this was not verified against a container.

## 1. Security (do first: it also unblocks the fast-path work)

- [ ] **Forged tokens accepted. One fix, two entry points.** [R1.1, R1.2, P8.1]
  - `SessionManager::clientHasValidSession` (`sessionmanager.cpp:156-186`) decodes the JWT without verifying the signature and looks the session up by `group_id`; it never compares the token with the one stored at login. Signature is only checked on a session miss.
  - Logout (`sessionmanager.cpp:102-118`) uses `jwt::decode` with no signature check (`ValidateToken` commented out), so a forged token can log any `clientId` out. `token.value()` at `:100` is unchecked: no token gives a `500` with raw `bad_optional_access` text.
  - Fix: look sessions up by a hash of the token (or compare the stored token in constant time) and take id, group and expiry from the stored session. A session hit then needs no JWT parse (this is also the speed win in [P8.1]). Keep signature verification in the slow path (`TokenManager::isTokenValid`). Make logout use the same lookup and return `401` when the token is missing.
  - Decide: must a session hit re-check `is_active`, or must suspend code remove the session? (Today a suspended client keeps its session.)
  - Tests: forged token on the auth filter, forged token on logout, missing token on logout; extend the login/logout API tests and `fuzz_auth.sh`.
- [ ] **Whitelist/blacklist and the rate-limit filter: one rework.** [R1.3, R2.4, P8.2]
  - Matching is far too broad: entries are unanchored regexes (`192.168.1.*` also matches `192.168.10.4`; `127.0.0.1` matches `127.0.0.10`-`.199`), and a whitelisted IP skips all DoS checks. Replace with exact-IP, CIDR and per-octet wildcard matchers (regex only behind a `re:` prefix), compiled once at startup into immutable, lock-free lists. This is the same change [P8.2] asks for (stop locking `const` sets), so do it once.
  - Open decisions: should the blacklist win over the whitelist (test `whitelist wins over blacklist` locks the current order in)? Is `X-Forwarded-For` needed? Behind Docker or a proxy `req->peerAddr()` (`ratelimit.hpp:27`) is the gateway, so every client shares one IP. Add opt-in trusted-proxy handling.
  - While in `ratelimit.hpp`: fix reply text/status (`RATELIMITED` says "IP is blacklisted." at `:45`; `BANNED` returns `503`, `429`/`403` fits) [R2.4]; hold the detector as a member instead of looking `GateKeeper` up in the Store per request; take one lock or shard by IP for counters; key by the 64-bit fingerprint; stop copying all headers into a new map; a rate-limited request calls `processRequest` twice [P8.2].
  - Tests: over-match cases (`192.168.1.50`, `127.0.0.10`), CIDR/wildcard, precedence, updated invalid-pattern test.
- [ ] **`staff/add` has no permission check.** `StaffController<T>::AddStaffToEntity` (`staffcontroller.cpp:40`) marks `requester` as `[[maybe_unused]]` and never calls `gateKeeper->canManageStaff<T>(requester, _id, error)`, which `RemoveStaffFromEntity` (`:71`) and `InviteStaffToEntity` (`:95`) both do. Any authenticated client can add staff to any clinic, pharmacy, laboratory or radiology center.
  - Fix: call `canManageStaff` before the insert and return 403 on failure, exactly as the other two do; remove `[[maybe_unused]]`.
  - Tests: `controllers/test_staff_controller.cpp`: a non-manager requester gets 403 on add; a manager still succeeds; extend `test_api.sh` with the same case. Re-check the per-endpoint query count afterwards (this adds one permission lookup: 1 query warm becomes 2 on a cold permission cache).
- [ ] **Startup log prints DB password and JWT secret.** `configurator.hpp:106`, `:187` [R1.4]
- [ ] **Native run with default `TOKEN_SECRET`.** Warn or refuse when it equals `Defaults::TokenParameters::TOKEN_SECRET_`. [R1.5]
- [ ] **Duplicate JSON keys.** Verify jsoncons rejects them; enable strict parsing if not. `validator.cpp:29-34`, `:82-86` [R1.6]

## 2. Performance

### Step 7: Verify the DB executor

- [ ] Check that mixed throughput is back at the Stage 4 level on the current script (`tools/k6/benchmark.js`) and that overload returns 503 quickly.

### Step 7b: Login burst starves reads (new, from the 4 latest runs)

| Run                                   | login req/s | reads alone req/s | reads during burst req/s | read p99 during (ms) |
| ------------------------------------- | ----------- | ----------------- | ------------------------ | -------------------- |
| stage-last / old-hash (3cab417-dirty) | 24.4 / 23.6 | 5395 / 4230       | 4075 / 4022              | 2.8 / 2.7            |
| s4 (ebd7530)                          | 719.4       | 5754              | 1984                     | 7.3                  |
| s5 (3cab417-dirty, worktree)          | 710         | 5211              | 1216                     | 17                   |

- The two hash variants differ by far more than noise from the first pair: about 30x login throughput, but reads fall 2x-4x. Decide the target: login at ~24/s protects reads but is slow for users; ~700/s needs a cap so password hashing cannot take cores from request threads.
- [ ] Rerun s4 and s5 three times each to see whether 1984 vs 1216 req/s is real. [new]
- [ ] Bound password-hash concurrency (`PASSWORD_WORKERS`) and re-test; goal: read p99 during burst within about 2x of reads-alone, login throughput stated as a documented figure. [relates to Stage 5, off-loop hashing and Argon2id]

### Step 8: Per-request cost on IO threads (profile first: 30 s at 32 VUs of `providers/read`)

- [ ] Session/permission caches write on every read: `MemCache::get` moves entries to front under a lock; `BasicPermissionCache::get` renews the timer and deep-copies JSON. Options: renew timer after half the TTL, return `shared_ptr<const json>`, shard by key hash. **Do together with the `MemCache` expiry bug:** it only expires from the back of the list (`memcache.hpp:205-207`), which a move-to-front design makes worse. [P8.4, R4.2]
- [ ] Response path: cache CORS header strings, compact JSON error bodies. `Allow` is now only sent by the 405 branch and `Cors::apply` is the single CORS writer; ST204 is resolved for Drogon-recognized methods, while parse-level TRACE 405s still lack `Allow`/CORS (see section 0). [P8.5, R3.2]
- [ ] `Store` race (`store.hpp:24-56`, separate locks in `getObject`/`registerObject`): fix with `Store::cachedObject` and stop calling `getObject` in constructors and methods. [R4.1, P8.7, R5 style item]
- Done when: no filter or middleware function above ~3% in the profile, and mixed throughput above Stage 4 on the current script.

### Step 8b: Avoidable per-request objects (do after Step 8; the filter work overlaps)

Source: per-route object count (about 16-24 objects per request, 1 locked `Store::getObject` on every route). Only objects that can live in a class or static are listed; parsed JSON, entity data, DB results, responses and the pooled connection handle are genuinely per-request and stay.

- [ ] 1. `static const Validator::Rule` at the 5 sites (`entitycontroller.cpp` create, read, update; `clientcontroller.cpp:106`; `cliniccontroller.cpp:22`). A `Rule` is a map of sets (about 4 allocations); `check()` is const, so concurrent reads are safe. Keep them `const` with a comment.
- [ ] 2. Cache the `{}_safe` table name per entity type (`entitycontroller.cpp:111`) as a function-local `static const std::string`.
- [ ] 3. `RateLimit` (`ratelimit.hpp`): hold `GateKeeper` as a member, resolved in the constructor, and use the declared but unused `dos_detector` member. Removes the only locked `Store::getObject` per request. Check the init order: the Store objects must exist before Drogon builds the filter.
- [ ] 4a. `DOSDetector::Request`: `method` and `path` become `string_view`; `ip` stays `std::string` (the IP lookups take `const std::string&`, and IPv6 would allocate on conversion). `processRequest` already copies `ip` and keeps only a fingerprint string, so no view outlives the call; document "valid only during `is_dos_attack`" on the struct.
- [ ] 4b. Replace the header map copy with a caller-computed `header_hash` (Drogon's header map is probably not `std::unordered_map<string,string>`; verify at compile time). Add `static DOSDetector::hashHeaders(range)`: XXH3 each header value, XOR the results, so the fingerprint no longer depends on header order. Use XXH3 streaming for the body so the 4 KB buffer in `generateRequestFingerprint` goes away.
- Dropped (no benefit or unsafe): reusing `DatabaseHanndler` across requests (it is RAII over a pooled connection; reuse leaks or shares connections); the `Bearer ` substring in `Auth` (fits the small-string buffer, no allocation).
- Expected gain: about 15-30 allocations per request, mostly from item 4b; likely 2-5% throughput, inside run-to-run noise. Judge it by allocations per request (`heaptrack` on a fixed 10k-request replay), not req/s.

**Tests for Step 8b** (write them first, against the current code):

- [ ] `gatekeeper/test_dosdetector.cpp`, edit: `makeRequest` and the fixture's `send()` build `Request` with a by-value `headers` map; switch them to `header_hash = DOSDetector::hashHeaders(headers)` and keep `path`/`method` views backed by literals or the fixture's stored strings (a by-value parameter would dangle). Keep every existing status test (ban, rate limit, whitelist, blacklist, rollover, concurrency) unchanged as the regression net.
- [ ] `gatekeeper/test_dosdetector.cpp`, add: same header values in a different order give the same fingerprint (repeat still counts toward MAX_REQUESTS); different values give different fingerprints; `hashHeaders` of an empty set is stable and differs from a one-header set; after `is_dos_attack` returns, destroying the request's backing strings does not change the next request's status (run under ASan).
- [ ] `api/test_filters.cpp`, add: `RateLimit` filter test through `runFilter`: allowed calls the chain; blacklisted gives 403, ratelimited 429, banned 503, each with the right body. If `setPeerAddr` (only on `HttpRequestImpl`) makes this brittle, extract the status-to-response mapping into a small function and test that.
- [ ] `api/test_filters.cpp`, add: constructing `RateLimit` before `GateKeeper` is registered fails loudly (guards the init order from item 3).
- [ ] `utils/test_store.cpp` or `store/test_store_cached.cpp`, add: a test-build-only counter on `Store::getObject`; one `RateLimit` pass must make zero calls.
- [ ] `validator/test_validator.cpp`, add: the same `static const Rule` called from several threads gives identical results.
- [ ] `controllers/test_entity_controller.cpp`, add: a read uses the `{table}_safe` name, and two entity types do not share a cached name.
- [ ] `benchmarks/test_benchmarks.cpp`, add a hidden `[.benchmark]` case for `is_dos_attack` with 20 headers, plus an allocation counter (override `operator new` in the benchmark target only). Run before and after.
- No change: `controllers/test_database_busy.cpp` (handler untouched), the `Auth` filter tests, `archive/sqlinjectiondetector/` (not built).
- Order: add the new tests, make items 1-4, update `makeRequest`, run the full suite, then run the DoS and `RateLimit` tests under ASan and TSan, then benchmark 3+ runs per variant.
- Done when: all tests pass under ASan and TSan, zero `Store::getObject` calls per request, and allocations per request are recorded in `docs/perf/`.

### Step 9: Database round trips and statements

- [ ] Count round trips per endpoint (`log_statement=all` for one `test_api.sh` run); targets: reads 1, writes 1-2. [P9.1]
- [ ] Single-statement writes use `pqxx::nontransaction` (no `BEGIN`/`COMMIT`). **Partly done:** some `Database` paths already do; the `executeQuery<json, pqxx::work>` path used by `databasecontroller.cpp:67` does not. [P9.2]
- [ ] Replace `checkItemExists` before insert with `INSERT ... ON CONFLICT DO NOTHING RETURNING id`. [P9.3]
- [ ] Prepared statements for login, permission and by-id queries, re-prepared after `reconnect`. [P9.4]
- [ ] **Related bug to fix in the same pass** (same code): `ClientController::Delete` calls `removeSession` after `EntityController::Delete` whatever the outcome (`clientcontroller.cpp:133-138`). `Suspend` (`:181`) does the same, which is fine for suspend but means a suspended client's session _is_ removed; that answers half of the `is_active` question in section 1 (re-check only matters for paths that don't go through `Suspend`). [R2.3]
- Done when: round-trip counts are recorded in `docs/perf/` and no endpoint wraps a single statement in a transaction.

## 3. Correctness bugs (independent of performance)

- [ ] `communicate.cpp` captures `curl` and `data` by reference in a `static` map, so later requests use dangling references (`communicate.cpp:24-30`). [R2.2]
- [ ] Appointment update/delete use the read permission query; no update/delete queries defined. Decide the intended rule (`permissionmanager.cpp:159-179`). [R2.5]
- [ ] Client validator coerces before type check (`validator.cpp:169`): numeric `phone` passes. [R2.6]

## 4. API conformance (Schemathesis)

- [ ] ST201: reject non-integer search numbers (`2.5` truncated by `as<size_t>()`) and unknown properties (`types.hpp`, `Search_t`); add unit tests. [R3.1]
- [ ] ST204 `Allow` header: resolved for Drogon-recognized methods; the 80 remaining failures are TRACE requests rejected during parsing before the pre-routing advice, so they return 405 without `Allow` or CORS headers.
- [ ] Re-run the conformance suite: ST201 unchecked here; ST204 reduced to the parse-level TRACE case only. [R3.3]

## 5. Logging

- [ ] `StaffData` uses `fmt::print` with no newline (`types.hpp:316`); use `Message::ErrorMessage`. [R5.5]
- [ ] Direct output in `src/gatekeeper/dosdetector/dosdetector.cpp:360` and `:366` (`fmt::print` in error paths).
- [ ] Direct output in `src/validator/databaseschema/databaseschema.cpp:59` (`fmt::print`).
- [ ] `src/main.cpp:17` uses `std::cerr` before the logger exists; probably acceptable.

## 6. Documentation

- [ ] Regenerate the OpenAPI docs: stale staff URL and clinic-only appointments (`build_docs.py:375-389`, `steps.py:66-93`; run `tools/docs/build.sh`, commit `docs/`). [R6.1]
- [ ] Check staff URLs against a running server. [R6.2]

## 7. Build and release

- [ ] Confirm a full image build with `conan.lock` (next deps build recompiles every dependency once). [R7.1]
- [ ] Tag a release so the new entrypoint persistence and version stamping reach users. [R7.2]

## 8. Shell scripts (Linux / macOS)

Linux is solid and macOS mostly fine; only the items below are worth doing.

**High impact** (file : problem : affects)

- [ ] **H6** `schemathesis_test.sh:47, 58-62`: `pip3 install --user` fails under PEP 668 (Debian 12+, Ubuntu 23.04+, Fedora, **Homebrew Python on macOS**). Use the venv approach already in `tools/docs/build.sh:22-27`. **macOS, newer Linux. Blocks the conformance re-run in section 4 on your Mac.**
- [ ] **H7** `python3` hard requirement in `fuzz_auth.sh:13`, `sqlmap_test.sh:62`, `schemathesis_test.sh:55`, `tools/docs/build.sh:20`, `valhalla.sh:1086,1092,1108`. Stock macOS ships only the Xcode shim. Add one shared "find python" helper.
- [ ] **H11** `db_init.sh:1`: no shebang, no `set -e`, resolves `init.sql` from the current directory. Fails with Exec format error when run directly. All platforms.
- [ ] **H12** `valhalla.sh:1092`: `outdated` hard-requires `python3`, but only the optional Hub-tag lookup at 1947 uses it (`registry_digest` does not). All platforms.

**Build script**

- [ ] `build.sh:1`: `#!/bin/bash` should be `#!/usr/bin/env bash`, and add `set -euo pipefail` (today a failed `conan`/`cmake` still runs `ctest` and starts a stale binary). **This one can silently invalidate perf and test runs, so do it early.**
