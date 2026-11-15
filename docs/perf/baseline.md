# Performance run: baseline

- Date: 2026-11-15T07:18:35Z
- Git: 0b8d24c-dirty
- Host: Darwin 27.2.0 arm64, 10 CPUs
- Target: http://127.0.0.1:8080 (existing server)
- Sweep: 20s per level, endpoint POST /clients/providers/read (auth + permission check + 1 DB read)

## Concurrency sweep

| VUs | req/s | median ms | p95 ms | p99 ms | failed | requests |
|-----|-------|-----------|--------|--------|--------|----------|
| 1 | 5311.6 | 0.2 | 0.2 | 0.2 | 0% | 106273 |
| 8 | 14373 | 0.5 | 0.7 | 0.8 | 0% | 287510 |
| 32 | 13607 | 2 | 4.1 | 4.6 | 0% | 272217 |
| 128 | 12932.4 | 9.4 | 11.6 | 19.3 | 0% | 258821 |

## Mixed benchmark (reads + crud + auth)

| scenario | req/s | median ms | p95 ms | p99 ms | failed | requests |
|----------|-------|-----------|--------|--------|--------|----------|
| all | 4004.2 | 17.2 | 32.4 | 47.2 | 0% | 360640 |

Per-scenario p95 and thresholds: `raw/baseline/benchmark.log`.

## Checks before trusting the numbers

- A non-zero *failed* column usually means 429/403 from the rate limiter or connection-pool timeouts, not slowness. Look at `raw/baseline/*.log` and the server log.
- Run on an idle machine, with the load generator and the server not competing for the same cores if possible.
- Compare only runs with the same duration, sweep levels and hardware.
