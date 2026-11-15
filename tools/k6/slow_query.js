// Does one slow database query slow down unrelated requests? (Step 7: blocking database work must not hold IO threads.)
//   k6 run -e READ_VUS=8 -e SLOW_VUS=4 -e PHASE=15s tools/k6/slow_query.js
// Two phases: plain reads of the providers table alone ("quiet"), then the same reads while SLOW_VUS clients read from the
// users table. tools/perf_baseline.sh holds a lock on the users table during the second phase, so those requests wait inside
// the database and the server threads that serve them are blocked. Compare the p99 of reads_quiet and reads_slow.
// Keep SLOW_VUS below DB_WORKERS and SERVER_THREADS; with enough slow requests to occupy every worker, everything waits.
// Env: BASE_URL, PROVIDER_USER, PROVIDER_PASS, USER_NAME, USER_PASS, READ_VUS, SLOW_VUS, PHASE
import http from 'k6/http';
import { check, fail } from 'k6';

const BASE = (__ENV.BASE_URL || 'http://127.0.0.1:8080') + '/api/v2';
const PROVIDER = { username: __ENV.PROVIDER_USER || 'provider', password: __ENV.PROVIDER_PASS || '1234@Abc!' };
const USER = { username: __ENV.USER_NAME || 'user1', password: __ENV.USER_PASS || '123Mm@p0' };
const JSON_H = { 'Content-Type': 'application/json' };
const READ_VUS = Number(__ENV.READ_VUS || 8);
const SLOW_VUS = Number(__ENV.SLOW_VUS || 4);
const PHASE = __ENV.PHASE || '15s';
const GAP = 5; // seconds between the phases
const PHASE_SECONDS = parseInt(PHASE, 10);
const START2 = `${PHASE_SECONDS + GAP}s`;

export const options = {
  scenarios: {
    reads_quiet: { executor: 'constant-vus', exec: 'reads', vus: READ_VUS, duration: PHASE },
    reads_slow: { executor: 'constant-vus', exec: 'reads', vus: READ_VUS, duration: PHASE, startTime: START2 },
    slow_users: { executor: 'constant-vus', exec: 'slow', vus: SLOW_VUS, duration: PHASE, startTime: START2, gracefulStop: '60s' },
  },
  summaryTrendStats: ['avg', 'min', 'med', 'p(90)', 'p(95)', 'p(99)', 'max'],
  // Always-true thresholds: k6 only exports per-scenario sub-metrics (used by tools/perf_baseline.sh) when a threshold names them.
  thresholds: {
    'http_req_duration{scenario:reads_quiet}': ['max>=0'],
    'http_req_duration{scenario:reads_slow}': ['max>=0'],
    'http_reqs{scenario:reads_quiet}': ['count>=0'],
    'http_reqs{scenario:reads_slow}': ['count>=0'],
    'http_reqs{scenario:slow_users}': ['count>=0'],
    'http_req_duration{scenario:slow_users}': ['max>=0'],
  },
};

export function setup() {
  const p = http.post(`${BASE}/clients/providers/login`, JSON.stringify(PROVIDER), { headers: JSON_H });
  if (p.status !== 200) fail(`provider login failed (${p.status}): ${String(p.body).slice(0, 200)}`);
  const u = http.post(`${BASE}/clients/users/login`, JSON.stringify(USER), { headers: JSON_H });
  if (u.status !== 200) fail(`user login failed (${u.status}): ${String(u.body).slice(0, 200)}`);
  return { providerToken: p.json('token'), providerId: p.json('client_id'), userToken: u.json('token'), userId: u.json('client_id') };
}

export function reads(d) {
  const res = http.post(`${BASE}/clients/providers/read`, JSON.stringify({ id: d.providerId, schema: [] }), {
    headers: { ...JSON_H, Authorization: `Bearer ${d.providerToken}` },
    tags: { name: 'providers_read' },
  });
  check(res, { 'read 200': (r) => r.status === 200 });
}

export function slow(d) {
  // 200 once the lock is released, 503 if the server's queue or pool is full; either way this is the "slow" traffic.
  const res = http.post(`${BASE}/clients/users/read`, JSON.stringify({ id: d.userId, schema: [] }), {
    headers: { ...JSON_H, Authorization: `Bearer ${d.userToken}` },
    tags: { name: 'users_read_slow' },
    timeout: '60s',
    responseCallback: http.expectedStatuses(200, 503),
  });
  check(res, { 'slow read 200 or 503': (r) => r.status === 200 || r.status === 503 });
}
