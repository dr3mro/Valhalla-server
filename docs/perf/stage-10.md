# Performance run: stage-10

- Date: 2026-11-15T10:47:49Z
- Git: 7789d7e-dirty
- Host: Darwin 27.2.0 arm64, 10 CPUs
- Target: http://127.0.0.1:8080 (existing server)
- Sweep: 20s per level, endpoint POST /clients/providers/read (auth + permission check + 1 DB read)

## Concurrency sweep

| VUs | req/s | median ms | p95 ms | p99 ms | failed | requests |
|-----|-------|-----------|--------|--------|--------|----------|
| 1 | 7475.9 | 0.1 | 0.1 | 0.2 | 0% | 149630 |
| 8 | 15312.6 | 0.3 | 3 | 4 | 0% | 306438 |
| 32 | 13541.6 | 0.3 | 27.3 | 29.5 | 0% | 271045 |
| 128 | 13366.9 | 0.3 | 125 | 133.9 | 0% | 267644 |

## Mixed benchmark (reads + crud + auth)

| scenario | req/s | median ms | p95 ms | p99 ms | failed | requests |
|----------|-------|-----------|--------|--------|--------|----------|
| all | 6966.3 | 0.6 | 97.6 | 220.5 | 0% | 627582 |

Per-scenario p95 and thresholds: `raw/stage-10/benchmark.log`.

## Login burst (read latency with and without simultaneous logins)

| phase | read req/s | read p95 ms | read p99 ms | login req/s |
|-------|-----------|-------------|-------------|-------------|
| reads alone | 3545 | 5.7 | 7.4 | - |
| reads during login burst | 3009.9 | 6.2 | 8.6 | 34 |

## Slow query (read latency while other requests wait on a locked table)

| phase | read req/s | read p95 ms | read p99 ms | slow req/s |
|-------|-----------|-------------|-------------|-----------|
| reads alone | 0 | 0 | 0 | - |
| reads while 4 requests are stuck | 0 | 0 | 0 | 0 |

Read p99 should stay close to the first row. If it follows the stuck requests, database work is still blocking request threads (or SLOW_VUS is not below DB_WORKERS and SERVER_THREADS).

## Checks before trusting the numbers

- A non-zero *failed* column usually means 429/403 from the rate limiter or connection-pool timeouts, not slowness. Look at `raw/stage-10/*.log` and the server log.
- Run on an idle machine, with the load generator and the server not competing for the same cores if possible.
- Compare only runs with the same duration, sweep levels and hardware.
