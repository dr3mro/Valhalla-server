# Performance run: stage-5

- Date: 2026-11-15T09:41:01Z
- Git: 33ef8fd-dirty
- Host: Darwin 27.2.0 arm64, 10 CPUs
- Target: http://127.0.0.1:8080 (existing server)
- Sweep: 20s per level, endpoint POST /clients/providers/read (auth + permission check + 1 DB read)

## Concurrency sweep

| VUs | req/s | median ms | p95 ms | p99 ms | failed | requests |
|-----|-------|-----------|--------|--------|--------|----------|
| 1 | 7903.5 | 0.1 | 0.1 | 0.1 | 0% | 158178 |
| 8 | 22807.4 | 0.3 | 0.5 | 0.7 | 0% | 456437 |
| 32 | 26084.6 | 1.1 | 2.2 | 2.8 | 0% | 522063 |
| 128 | 24765.3 | 4.6 | 9.1 | 10.9 | 0% | 495756 |

## Mixed benchmark (reads + crud + auth)

| scenario | req/s | median ms | p95 ms | p99 ms | failed | requests |
|----------|-------|-----------|--------|--------|--------|----------|
| all | 10175.8 | 6.2 | 16 | 23.1 | 0% | 917007 |

Per-scenario p95 and thresholds: `raw/stage-5/benchmark.log`.

## Login burst (read latency with and without simultaneous logins)

| phase | read req/s | read p95 ms | read p99 ms | login req/s |
|-------|-----------|-------------|-------------|-------------|
| reads alone | 5527.1 | 0.8 | 1.3 | - |
| reads during login burst | 4663.3 | 1.4 | 2.5 | 25.2 |

## Checks before trusting the numbers

- A non-zero *failed* column usually means 429/403 from the rate limiter or connection-pool timeouts, not slowness. Look at `raw/stage-5/*.log` and the server log.
- Run on an idle machine, with the load generator and the server not competing for the same cores if possible.
- Compare only runs with the same duration, sweep levels and hardware.
