# Performance run: stage-last

- Date: 2026-11-15T11:19:48Z
- Git: 408129c-dirty
- Host: Darwin 27.2.0 arm64, 10 CPUs
- Target: http://127.0.0.1:8080 (existing server)
- Sweep: 20s per level, endpoint POST /clients/providers/read (auth + permission check + 1 DB read)

## Concurrency sweep

| VUs | req/s | median ms | p95 ms | p99 ms | failed | requests |
|-----|-------|-----------|--------|--------|--------|----------|
| 1 | 8088.8 | 0.1 | 0.1 | 0.1 | 0% | 161890 |
| 8 | 20346.6 | 0.3 | 0.5 | 0.6 | 0% | 407190 |
| 32 | 21074.5 | 1.4 | 2.7 | 3.4 | 0% | 421829 |
| 128 | 15734 | 7.3 | 14.2 | 18 | 0% | 315128 |

## Mixed benchmark (reads + crud + auth)

| scenario | req/s | median ms | p95 ms | p99 ms | failed | requests |
|----------|-------|-----------|--------|--------|--------|----------|
| all | 8446.7 | 7.8 | 17.3 | 23.1 | 0% | 761405 |

Per-scenario p95 and thresholds: `raw/stage-last/benchmark.log`.

## Login burst (read latency with and without simultaneous logins)

| phase | read req/s | read p95 ms | read p99 ms | login req/s |
|-------|-----------|-------------|-------------|-------------|
| reads alone | 5337.4 | 0.9 | 1.3 | - |
| reads during login burst | 4154.2 | 1.5 | 2.9 | 24.1 |

## Slow query (read latency while other requests wait on a locked table)

| phase | read req/s | read p95 ms | read p99 ms | slow req/s |
|-------|-----------|-------------|-------------|-----------|
| reads alone | 5502.4 | 0.9 | 1.2 | - |
| reads while 4 requests are stuck | 5141.4 | 0.7 | 1.1 | 234.1 |

Read p99 should stay close to the first row. If it follows the stuck requests, database work is still blocking request threads (or SLOW_VUS is not below DB_WORKERS and SERVER_THREADS).

## Checks before trusting the numbers

- A non-zero *failed* column usually means 429/403 from the rate limiter or connection-pool timeouts, not slowness. Look at `raw/stage-last/*.log` and the server log.
- Run on an idle machine, with the load generator and the server not competing for the same cores if possible.
- Compare only runs with the same duration, sweep levels and hardware.
