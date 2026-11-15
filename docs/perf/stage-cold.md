# Performance run: stage-cold

- Date: 2026-11-15T12:46:17Z
- Git: 4b089a0-dirty
- Host: Darwin 27.2.0 arm64, 10 CPUs
- Target: http://127.0.0.1:8080 (existing server)
- Sweep: 20s per level, endpoint POST /clients/providers/read (auth + permission check + 1 DB read)

## Concurrency sweep

| VUs | req/s | median ms | p95 ms | p99 ms | failed | requests |
|-----|-------|-----------|--------|--------|--------|----------|
| 1 | 7395.4 | 0.1 | 0.1 | 0.2 | 0% | 147937 |
| 8 | 22485.9 | 0.3 | 0.4 | 0.6 | 0% | 449785 |
| 32 | 26187.5 | 1.1 | 1.8 | 3 | 0% | 523847 |
| 128 | 25047.7 | 4.8 | 7.1 | 9.7 | 0% | 501101 |

## Mixed benchmark (reads + crud + auth)

| scenario | req/s | median ms | p95 ms | p99 ms | failed | requests |
|----------|-------|-----------|--------|--------|--------|----------|
| all | 17833.6 | 3.5 | 7.3 | 14.5 | 0% | 1606010 |

Per-scenario p95 and thresholds: `raw/stage-cold/benchmark.log`.

## Login burst (read latency with and without simultaneous logins)

| phase | read req/s | read p95 ms | read p99 ms | login req/s |
|-------|-----------|-------------|-------------|-------------|
| reads alone | 6549.3 | 0.7 | 1 | - |
| reads during login burst | 5231.8 | 1.1 | 1.5 | 27.3 |

## Slow query (read latency while other requests wait on a locked table)

| phase | read req/s | read p95 ms | read p99 ms | slow req/s |
|-------|-----------|-------------|-------------|-----------|
| reads alone | 6494.9 | 0.7 | 1 | - |
| reads while 4 requests are stuck | 7042 | 0.7 | 0.9 | 226.6 |

Read p99 should stay close to the first row. If it follows the stuck requests, database work is still blocking request threads (or SLOW_VUS is not below DB_WORKERS and SERVER_THREADS).

## Checks before trusting the numbers

- A non-zero *failed* column usually means 429/403 from the rate limiter or connection-pool timeouts, not slowness. Look at `raw/stage-cold/*.log` and the server log.
- Run on an idle machine, with the load generator and the server not competing for the same cores if possible.
- Compare only runs with the same duration, sweep levels and hardware.
