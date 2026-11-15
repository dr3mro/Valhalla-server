# Performance run: stage-9

- Date: 2026-11-15T10:27:20Z
- Git: 2dd29b3-dirty
- Host: Darwin 27.2.0 arm64, 10 CPUs
- Target: http://127.0.0.1:8080 (existing server)
- Sweep: 20s per level, endpoint POST /clients/providers/read (auth + permission check + 1 DB read)

## Concurrency sweep

| VUs | req/s | median ms | p95 ms | p99 ms | failed | requests |
|-----|-------|-----------|--------|--------|--------|----------|
| 1 | 7456.6 | 0.1 | 0.1 | 0.2 | 0% | 149241 |
| 8 | 21805.9 | 0.3 | 0.4 | 0.6 | 0% | 436398 |
| 32 | 24933.5 | 1.2 | 1.6 | 2.2 | 0% | 499030 |
| 128 | 22976.2 | 5.4 | 7.2 | 8.1 | 0% | 459935 |

## Mixed benchmark (reads + crud + auth)

| scenario | req/s | median ms | p95 ms | p99 ms | failed | requests |
|----------|-------|-----------|--------|--------|--------|----------|
| all | 10151.1 | 6.9 | 13.3 | 16.7 | 0% | 914757 |

Per-scenario p95 and thresholds: `raw/stage-9/benchmark.log`.

## Login burst (read latency with and without simultaneous logins)

| phase | read req/s | read p95 ms | read p99 ms | login req/s |
|-------|-----------|-------------|-------------|-------------|
| reads alone | 5515.6 | 0.8 | 1.2 | - |
| reads during login burst | 4619.5 | 1.3 | 2.1 | 25.2 |

## Checks before trusting the numbers

- A non-zero *failed* column usually means 429/403 from the rate limiter or connection-pool timeouts, not slowness. Look at `raw/stage-9/*.log` and the server log.
- Run on an idle machine, with the load generator and the server not competing for the same cores if possible.
- Compare only runs with the same duration, sweep levels and hardware.
