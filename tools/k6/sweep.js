// Concurrency sweep for one read endpoint (used by tools/perf_baseline.sh).
//   k6 run -e VUS=32 -e DURATION=20s tools/k6/sweep.js
// Env: BASE_URL, PROVIDER_USER, PROVIDER_PASS, VUS, DURATION
import http from 'k6/http';
import { check, fail } from 'k6';

const BASE = (__ENV.BASE_URL || 'http://127.0.0.1:8080') + '/api/v2';
const CRED = { username: __ENV.PROVIDER_USER || 'provider', password: __ENV.PROVIDER_PASS || '1234@Abc!' };
const JSON_H = { 'Content-Type': 'application/json' };

export const options = {
  vus: Number(__ENV.VUS || 8),
  duration: __ENV.DURATION || '20s',
  summaryTrendStats: ['avg', 'min', 'med', 'p(90)', 'p(95)', 'p(99)', 'max'],
};

export function setup() {
  const r = http.post(`${BASE}/clients/providers/login`, JSON.stringify(CRED), { headers: JSON_H });
  if (r.status !== 200) fail(`login failed (${r.status}): ${String(r.body).slice(0, 200)}`);
  return { token: r.json('token'), id: r.json('client_id') };
}

export default function (d) {
  const res = http.post(`${BASE}/clients/providers/read`, JSON.stringify({ id: d.id, schema: [] }), {
    headers: { ...JSON_H, Authorization: `Bearer ${d.token}` },
    tags: { name: 'providers_read' },
  });
  check(res, { 'read 200': (r) => r.status === 200 });
}
