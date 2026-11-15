# Performance run: stage-7

- Date: 2026-11-15T10:03:44Z
- Git: 2dd29b3-dirty
- Host: Darwin 27.2.0 arm64, 10 CPUs
- Target: http://127.0.0.1:8080 (existing server)
- Sweep: 20s per level, endpoint POST /clients/providers/read (auth + permission check + 1 DB read)

## Concurrency sweep

| VUs | req/s | median ms | p95 ms | p99 ms | failed | requests |
|-----|-------|-----------|--------|--------|--------|----------|
| 1 | 7473.3 | 0.1 | 0.1 | 0.2 | 0% | 149574 |
| 8 | 21383.5 | 0.3 | 0.5 | 0.6 | 0% | 427942 |
| 32 | 24346.7 | 1.2 | 1.7 | 2.2 | 0% | 487286 |
| 128 | 22328.2 | 5.5 | 7.5 | 8.3 | 0% | 446966 |

## Mixed benchmark (reads + crud + auth)

| scenario | req/s | median ms | p95 ms | p99 ms | failed | requests |
|----------|-------|-----------|--------|--------|--------|----------|
| all | 9618.6 | 7.2 | 14.2 | 17.9 | 0% | 866532 |

Per-scenario p95 and thresholds: `raw/stage-7/benchmark.log`.

## Login burst (read latency with and without simultaneous logins)

| phase | read req/s | read p95 ms | read p99 ms | login req/s |
|-------|-----------|-------------|-------------|-------------|
| reads alone | 5120.7 | 0.9 | 1.3 | - |
| reads during login burst | 4221.1 | 1.5 | 2.6 | 24.2 |

## Checks before trusting the numbers

- A non-zero *failed* column usually means 429/403 from the rate limiter or connection-pool timeouts, not slowness. Look at `raw/stage-7/*.log` and the server log.
- Run on an idle machine, with the load generator and the server not competing for the same cores if possible.
- Compare only runs with the same duration, sweep levels and hardware.
