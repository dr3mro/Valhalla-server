# Performance run: stage-8

- Date: 2026-11-15T10:10:55Z
- Git: 2dd29b3-dirty
- Host: Darwin 27.2.0 arm64, 10 CPUs
- Target: http://127.0.0.1:8080 (existing server)
- Sweep: 20s per level, endpoint POST /clients/providers/read (auth + permission check + 1 DB read)

## Concurrency sweep

| VUs | req/s | median ms | p95 ms | p99 ms | failed | requests |
|-----|-------|-----------|--------|--------|--------|----------|
| 1 | 7869.6 | 0.1 | 0.1 | 0.1 | 0% | 157498 |
| 8 | 23758.8 | 0.3 | 0.4 | 0.6 | 0% | 475481 |
| 32 | 27571.8 | 1.1 | 2.1 | 2.5 | 0% | 551825 |
| 128 | 26113.1 | 4.4 | 8.6 | 10.3 | 0% | 522729 |

## Mixed benchmark (reads + crud + auth)

| scenario | req/s | median ms | p95 ms | p99 ms | failed | requests |
|----------|-------|-----------|--------|--------|--------|----------|
| all | 10861.8 | 5.7 | 15.4 | 22.2 | 0% | 978499 |

Per-scenario p95 and thresholds: `raw/stage-8/benchmark.log`.

## Login burst (read latency with and without simultaneous logins)

| phase | read req/s | read p95 ms | read p99 ms | login req/s |
|-------|-----------|-------------|-------------|-------------|
| reads alone | 5599.1 | 0.8 | 1.2 | - |
| reads during login burst | 4580.7 | 1.4 | 2.3 | 24.5 |

## Checks before trusting the numbers

- A non-zero *failed* column usually means 429/403 from the rate limiter or connection-pool timeouts, not slowness. Look at `raw/stage-8/*.log` and the server log.
- Run on an idle machine, with the load generator and the server not competing for the same cores if possible.
- Compare only runs with the same duration, sweep levels and hardware.
