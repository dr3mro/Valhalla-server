# Performance run: stage-last

- Date: 2026-11-15T11:42:49Z
- Git: 3cab417-dirty
- Host: Darwin 27.2.0 arm64, 10 CPUs
- Target: http://127.0.0.1:8080 (existing server)
- Sweep: 20s per level, endpoint POST /clients/providers/read (auth + permission check + 1 DB read)

## Concurrency sweep

| VUs | req/s | median ms | p95 ms | p99 ms | failed | requests |
|-----|-------|-----------|--------|--------|--------|----------|
| 1 | 7438.6 | 0.1 | 0.1 | 0.2 | 0% | 148878 |
| 8 | 21892.4 | 0.3 | 0.4 | 0.6 | 0% | 438110 |
| 32 | 24795.2 | 1.2 | 1.9 | 3 | 0% | 496261 |
| 128 | 22571 | 5.3 | 8.2 | 14.2 | 0% | 451851 |

## Mixed benchmark (reads + crud + auth)

| scenario | req/s | median ms | p95 ms | p99 ms | failed | requests |
|----------|-------|-----------|--------|--------|--------|----------|
| all | 10309.6 | 5.9 | 14.2 | 28.2 | 0% | 928928 |

Per-scenario p95 and thresholds: `raw/stage-last/benchmark.log`.

## Login burst (read latency with and without simultaneous logins)

| phase | read req/s | read p95 ms | read p99 ms | login req/s |
|-------|-----------|-------------|-------------|-------------|
| reads alone | 5395 | 0.9 | 1.2 | - |
| reads during login burst | 4074.8 | 1.5 | 2.8 | 24.4 |

## Slow query (read latency while other requests wait on a locked table)

| phase | read req/s | read p95 ms | read p99 ms | slow req/s |
|-------|-----------|-------------|-------------|-----------|
| reads alone | 5526.7 | 0.8 | 1.2 | - |
| reads while 4 requests are stuck | 5879.9 | 0.9 | 1.3 | 232.9 |

Read p99 should stay close to the first row. If it follows the stuck requests, database work is still blocking request threads (or SLOW_VUS is not below DB_WORKERS and SERVER_THREADS).

## Checks before trusting the numbers

- A non-zero *failed* column usually means 429/403 from the rate limiter or connection-pool timeouts, not slowness. Look at `raw/stage-last/*.log` and the server log.
- Run on an idle machine, with the load generator and the server not competing for the same cores if possible.
- Compare only runs with the same duration, sweep levels and hardware.
