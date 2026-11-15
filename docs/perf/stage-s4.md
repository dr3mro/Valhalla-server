# Performance run: stage-s4

- Date: 2026-11-15T12:08:47Z
- Git: ebd7530
- Host: Darwin 27.2.0 arm64, 10 CPUs
- Target: http://127.0.0.1:8080 (existing server)
- Sweep: 20s per level, endpoint POST /clients/providers/read (auth + permission check + 1 DB read)

## Concurrency sweep

| VUs | req/s | median ms | p95 ms | p99 ms | failed | requests |
|-----|-------|-----------|--------|--------|--------|----------|
| 1 | 7791.4 | 0.1 | 0.1 | 0.1 | 0% | 155870 |
| 8 | 23350.2 | 0.3 | 0.4 | 0.5 | 0% | 467069 |
| 32 | 25731.8 | 1.2 | 2.2 | 2.9 | 0% | 514732 |
| 128 | 21552.4 | 5.3 | 10.4 | 12.9 | 0% | 431233 |

## Mixed benchmark (reads + crud + auth)

| scenario | req/s | median ms | p95 ms | p99 ms | failed | requests |
|----------|-------|-----------|--------|--------|--------|----------|
| all | 10540.4 | 6.3 | 14.9 | 20.3 | 0% | 949233 |

Per-scenario p95 and thresholds: `raw/stage-s4/benchmark.log`.

## Login burst (read latency with and without simultaneous logins)

| phase | read req/s | read p95 ms | read p99 ms | login req/s |
|-------|-----------|-------------|-------------|-------------|
| reads alone | 5753.9 | 0.8 | 1.2 | - |
| reads during login burst | 1983.8 | 5.4 | 7.3 | 719.4 |

## Slow query (read latency while other requests wait on a locked table)

| phase | read req/s | read p95 ms | read p99 ms | slow req/s |
|-------|-----------|-------------|-------------|-----------|
| reads alone | 6077.3 | 0.8 | 1.1 | - |
| reads while 4 requests are stuck | 5720 | 0.9 | 1.3 | 259.9 |

Read p99 should stay close to the first row. If it follows the stuck requests, database work is still blocking request threads (or SLOW_VUS is not below DB_WORKERS and SERVER_THREADS).

## Checks before trusting the numbers

- A non-zero *failed* column usually means 429/403 from the rate limiter or connection-pool timeouts, not slowness. Look at `raw/stage-s4/*.log` and the server log.
- Run on an idle machine, with the load generator and the server not competing for the same cores if possible.
- Compare only runs with the same duration, sweep levels and hardware.
