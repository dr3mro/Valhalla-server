# Performance run: stage-last-s5

- Date: 2026-11-15T11:48:40Z
- Git: 3cab417-dirty
- Host: Darwin 27.2.0 arm64, 10 CPUs
- Target: http://127.0.0.1:8080 (existing server)
- Sweep: 20s per level, endpoint POST /clients/providers/read (auth + permission check + 1 DB read)

## Concurrency sweep

| VUs | req/s | median ms | p95 ms | p99 ms | failed | requests |
|-----|-------|-----------|--------|--------|--------|----------|
| 1 | 7905 | 0.1 | 0.1 | 0.1 | 0% | 158122 |
| 8 | 22261 | 0.3 | 0.5 | 0.7 | 0% | 445282 |
| 32 | 24434.4 | 1.2 | 2.4 | 3.1 | 0% | 488783 |
| 128 | 20973 | 5.5 | 10.7 | 13.1 | 0% | 419656 |

## Mixed benchmark (reads + crud + auth)

| scenario | req/s | median ms | p95 ms | p99 ms | failed | requests |
|----------|-------|-----------|--------|--------|--------|----------|
| all | 8952.1 | 7.3 | 17.5 | 24.7 | 0% | 806141 |

Per-scenario p95 and thresholds: `raw/stage-last-s5/benchmark.log`.

## Login burst (read latency with and without simultaneous logins)

| phase | read req/s | read p95 ms | read p99 ms | login req/s |
|-------|-----------|-------------|-------------|-------------|
| reads alone | 5211.2 | 1 | 1.4 | - |
| reads during login burst | 1215.7 | 8.1 | 17 | 710 |

## Slow query (read latency while other requests wait on a locked table)

| phase | read req/s | read p95 ms | read p99 ms | slow req/s |
|-------|-----------|-------------|-------------|-----------|
| reads alone | 5382.5 | 0.9 | 1.3 | - |
| reads while 4 requests are stuck | 5323.3 | 0.9 | 1.3 | 267.3 |

Read p99 should stay close to the first row. If it follows the stuck requests, database work is still blocking request threads (or SLOW_VUS is not below DB_WORKERS and SERVER_THREADS).

## Checks before trusting the numbers

- A non-zero *failed* column usually means 429/403 from the rate limiter or connection-pool timeouts, not slowness. Look at `raw/stage-last-s5/*.log` and the server log.
- Run on an idle machine, with the load generator and the server not competing for the same cores if possible.
- Compare only runs with the same duration, sweep levels and hardware.
