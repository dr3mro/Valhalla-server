# Performance run: stage-1

- Date: 2026-11-15T07:54:54Z
- Git: b498fa3-dirty
- Host: Darwin 27.2.0 arm64, 10 CPUs
- Target: http://127.0.0.1:8080 (existing server)
- Sweep: 20s per level, endpoint POST /clients/providers/read (auth + permission check + 1 DB read)

## Concurrency sweep

| VUs | req/s | median ms | p95 ms | p99 ms | failed | requests |
|-----|-------|-----------|--------|--------|--------|----------|
| 1 | 5299.3 | 0.2 | 0.2 | 0.2 | 0% | 106009 |
| 8 | 18205.2 | 0.4 | 0.5 | 0.7 | 0% | 364165 |
| 32 | 19209.3 | 1.6 | 2.9 | 3.6 | 0% | 384272 |
| 128 | 19327.7 | 6 | 11.8 | 13.6 | 0% | 386733 |

## Mixed benchmark (reads + crud + auth)

| scenario | req/s | median ms | p95 ms | p99 ms | failed | requests |
|----------|-------|-----------|--------|--------|--------|----------|
| all | 5456.7 | 11.8 | 28.4 | 41.5 | 0% | 491442 |

Per-scenario p95 and thresholds: `raw/stage-1/benchmark.log`.

## Checks before trusting the numbers

- A non-zero *failed* column usually means 429/403 from the rate limiter or connection-pool timeouts, not slowness. Look at `raw/stage-1/*.log` and the server log.
- Run on an idle machine, with the load generator and the server not competing for the same cores if possible.
- Compare only runs with the same duration, sweep levels and hardware.
