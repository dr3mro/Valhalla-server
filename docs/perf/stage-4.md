# Performance run: stage-4

- Date: 2026-11-15T09:18:19Z
- Git: bc451bd-dirty
- Host: Darwin 27.2.0 arm64, 10 CPUs
- Target: http://127.0.0.1:8080 (existing server)
- Sweep: 20s per level, endpoint POST /clients/providers/read (auth + permission check + 1 DB read)

## Concurrency sweep

| VUs | req/s | median ms | p95 ms | p99 ms | failed | requests |
|-----|-------|-----------|--------|--------|--------|----------|
| 1 | 7901.6 | 0.1 | 0.1 | 0.2 | 0% | 158084 |
| 8 | 23885.7 | 0.3 | 0.4 | 0.5 | 0% | 477793 |
| 32 | 27050.6 | 1.1 | 2.1 | 2.6 | 0% | 541116 |
| 128 | 26124.6 | 4.4 | 8.6 | 10.3 | 0% | 522691 |

## Mixed benchmark (reads + crud + auth)

| scenario | req/s | median ms | p95 ms | p99 ms | failed | requests |
|----------|-------|-----------|--------|--------|--------|----------|
| all | 14229.6 | 4.5 | 11.1 | 15.7 | 0% | 1281209 |

Per-scenario p95 and thresholds: `raw/stage-4/benchmark.log`.

## Checks before trusting the numbers

- A non-zero *failed* column usually means 429/403 from the rate limiter or connection-pool timeouts, not slowness. Look at `raw/stage-4/*.log` and the server log.
- Run on an idle machine, with the load generator and the server not competing for the same cores if possible.
- Compare only runs with the same duration, sweep levels and hardware.
