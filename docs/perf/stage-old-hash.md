# Performance run: stage-old-hash

- Date: 2026-11-15T11:59:19Z
- Git: 3cab417-dirty
- Host: Darwin 27.2.0 arm64, 10 CPUs
- Target: http://127.0.0.1:8080 (existing server)
- Sweep: 20s per level, endpoint POST /clients/providers/read (auth + permission check + 1 DB read)

## Concurrency sweep

| VUs | req/s | median ms | p95 ms | p99 ms | failed | requests |
|-----|-------|-----------|--------|--------|--------|----------|
| 1 | 7463.5 | 0.1 | 0.1 | 0.2 | 0% | 149364 |
| 8 | 21453.4 | 0.3 | 0.5 | 0.6 | 0% | 429338 |
| 32 | 24089.6 | 1.2 | 2 | 3.5 | 0% | 482117 |
| 128 | 23271 | 5.1 | 7.7 | 12.7 | 0% | 465902 |

## Mixed benchmark (reads + crud + auth)

| scenario | req/s | median ms | p95 ms | p99 ms | failed | requests |
|----------|-------|-----------|--------|--------|--------|----------|
| all | 9684.5 | 5.6 | 16.6 | 38.2 | 0% | 872492 |

Per-scenario p95 and thresholds: `raw/stage-old-hash/benchmark.log`.

## Login burst (read latency with and without simultaneous logins)

| phase | read req/s | read p95 ms | read p99 ms | login req/s |
|-------|-----------|-------------|-------------|-------------|
| reads alone | 4230.3 | 1.3 | 1.9 | - |
| reads during login burst | 4021.7 | 1.6 | 2.7 | 23.6 |

## Slow query (read latency while other requests wait on a locked table)

| phase | read req/s | read p95 ms | read p99 ms | slow req/s |
|-------|-----------|-------------|-------------|-----------|
| reads alone | 5185.5 | 1 | 1.5 | - |
| reads while 4 requests are stuck | 5931.4 | 0.8 | 1.2 | 230.5 |

Read p99 should stay close to the first row. If it follows the stuck requests, database work is still blocking request threads (or SLOW_VUS is not below DB_WORKERS and SERVER_THREADS).

## Checks before trusting the numbers

- A non-zero *failed* column usually means 429/403 from the rate limiter or connection-pool timeouts, not slowness. Look at `raw/stage-old-hash/*.log` and the server log.
- Run on an idle machine, with the load generator and the server not competing for the same cores if possible.
- Compare only runs with the same duration, sweep levels and hardware.
