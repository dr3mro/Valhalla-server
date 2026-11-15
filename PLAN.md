# Valhalla Server: Combined Plan (remaining work + performance)

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
- [ ] Response path: cache CORS header strings, compact JSON error bodies. **Do together with ST204:** stop sending `Allow` on normal responses and make the `405` list and `addCorsHeaders` consistent (`server.cpp:69-71`, `:115`). That removes a per-response header and fixes the conformance failure in one change. [P8.5, R3.2]
- [ ] `Store` race (`store.hpp:24-56`, separate locks in `getObject`/`registerObject`): fix with `Store::cachedObject` and stop calling `getObject` in constructors and methods. [R4.1, P8.7, R5 style item]
- Done when: no filter or middleware function above ~3% in the profile, and mixed throughput above Stage 4 on the current script.

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
- [ ] ST204 `Allow` header: handled in Step 8 (response path).
- [ ] Re-run the conformance suite: zero ST201 and ST204 failures. [R3.3]

## 5. Logging

- [ ] `StaffData` uses `fmt::print` with no newline (`types.hpp:316`); use `Message::ErrorMessage`. [R5.5]

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
