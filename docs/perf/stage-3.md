# Performance run: stage-3

- Date: 2026-11-15T08:40:46Z
- Git: f7939ae-dirty
- Host: Darwin 27.2.0 arm64, 10 CPUs
- Target: http://127.0.0.1:8080 (existing server)
- Sweep: 20s per level, endpoint POST /clients/providers/read (auth + permission check + 1 DB read)

## Concurrency sweep

| VUs | req/s | median ms | p95 ms | p99 ms | failed | requests |
|-----|-------|-----------|--------|--------|--------|----------|
| 1 | 7873 | 0.1 | 0.1 | 0.1 | 0% | 157499 |
| 8 | 23129 | 0.3 | 0.4 | 0.6 | 0% | 462649 |
| 32 | 25824.6 | 1.2 | 2.2 | 2.8 | 0% | 516587 |
| 128 | 25604.1 | 4.5 | 8.8 | 10.6 | 0% | 512281 |

## Mixed benchmark (reads + crud + auth)

| scenario | req/s | median ms | p95 ms | p99 ms | failed | requests |
|----------|-------|-----------|--------|--------|--------|----------|
| all | 13372.7 | 4.7 | 11.9 | 16.9 | 0% | 1204091 |

Per-scenario p95 and thresholds: `raw/stage-3/benchmark.log`.

## Checks before trusting the numbers

- A non-zero *failed* column usually means 429/403 from the rate limiter or connection-pool timeouts, not slowness. Look at `raw/stage-3/*.log` and the server log.
- Run on an idle machine, with the load generator and the server not competing for the same cores if possible.
- Compare only runs with the same duration, sweep levels and hardware.
