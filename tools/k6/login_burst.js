// Does a burst of logins slow down unrelated reads? (Step 6: password hashing must not run on the request threads.)
//   k6 run -e READ_VUS=8 -e LOGIN_VUS=16 -e PHASE=15s tools/k6/login_burst.js
// Two phases of the same read load: first alone ("quiet"), then while many clients log in at once ("burst").
// Compare p99 of reads_quiet and reads_burst; they should stay close.
// Env: BASE_URL, PROVIDER_USER, PROVIDER_PASS, READ_VUS, LOGIN_VUS, PHASE
import http from 'k6/http';
import { check, fail } from 'k6';

const BASE = (__ENV.BASE_URL || 'http://127.0.0.1:8080') + '/api/v2';
const CRED = { username: __ENV.PROVIDER_USER || 'provider', password: __ENV.PROVIDER_PASS || '1234@Abc!' };
const JSON_H = { 'Content-Type': 'application/json' };
const READ_VUS = Number(__ENV.READ_VUS || 8);
const LOGIN_VUS = Number(__ENV.LOGIN_VUS || 16);
const PHASE = __ENV.PHASE || '15s';
const GAP = '5s'; // pause between the phases
const PHASE_SECONDS = parseInt(PHASE, 10);

export const options = {
  scenarios: {
    reads_quiet: { executor: 'constant-vus', exec: 'reads', vus: READ_VUS, duration: PHASE },
    reads_burst: { executor: 'constant-vus', exec: 'reads', vus: READ_VUS, duration: PHASE, startTime: `${PHASE_SECONDS + parseInt(GAP, 10)}s` },
    logins: { executor: 'constant-vus', exec: 'logins', vus: LOGIN_VUS, duration: PHASE, startTime: `${PHASE_SECONDS + parseInt(GAP, 10)}s` },
  },
  summaryTrendStats: ['avg', 'min', 'med', 'p(90)', 'p(95)', 'p(99)', 'max'],
  // Always-true thresholds: k6 only exports per-scenario sub-metrics (used by tools/perf_baseline.sh) when a threshold names them.
  thresholds: {
    'http_req_duration{scenario:reads_quiet}': ['max>=0'],
    'http_req_duration{scenario:reads_burst}': ['max>=0'],
    'http_reqs{scenario:reads_quiet}': ['count>=0'],
    'http_reqs{scenario:reads_burst}': ['count>=0'],
    'http_reqs{scenario:logins}': ['count>=0'],
  },
};

export function setup() {
  const r = http.post(`${BASE}/clients/providers/login`, JSON.stringify(CRED), { headers: JSON_H });
  if (r.status !== 200) fail(`login failed (${r.status}): ${String(r.body).slice(0, 200)}`);
  return { token: r.json('token'), id: r.json('client_id') };
}

export function reads(d) {
  const res = http.post(`${BASE}/clients/providers/read`, JSON.stringify({ id: d.id, schema: [] }), {
    headers: { ...JSON_H, Authorization: `Bearer ${d.token}` },
    tags: { name: 'providers_read' },
  });
  check(res, { 'read 200': (r) => r.status === 200 });
}

export function logins() {
  // 200 = logged in, 503 = the password queue was full (acceptable under a burst, it must not hurt the reads)
  const res = http.post(`${BASE}/clients/providers/login`, JSON.stringify(CRED), {
    headers: JSON_H,
    tags: { name: 'providers_login' },
    responseCallback: http.expectedStatuses(200, 503),
  });
  check(res, { 'login 200 or 503': (r) => r.status === 200 || r.status === 503 });
}
