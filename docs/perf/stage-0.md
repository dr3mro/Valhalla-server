# Performance run: stage-0

- Date: 2026-11-15T07:11:31Z
- Git: 091c69d
- Host: Darwin 27.2.0 arm64, 10 CPUs
- Target: http://127.0.0.1:8080 (existing server)
- Sweep: 20s per level, endpoint POST /clients/providers/read (auth + permission check + 1 DB read)

## Concurrency sweep

| VUs | req/s | median ms | p95 ms | p99 ms | failed | requests |
|-----|-------|-----------|--------|--------|--------|----------|
| 1 | 5363.3 | 0.2 | 0.2 | 0.2 | 0% | 107286 |
| 8 | 14790.5 | 0.5 | 0.6 | 0.8 | 0% | 295857 |
| 32 | 13800.6 | 2 | 4.1 | 4.5 | 0% | 276079 |
| 128 | 13389.1 | 9.1 | 11.2 | 18.6 | 0% | 267949 |

## Mixed benchmark (reads + crud + auth)

| scenario | req/s | median ms | p95 ms | p99 ms | failed | requests |
|----------|-------|-----------|--------|--------|--------|----------|
| all | 5114.9 | 13.8 | 23.6 | 34.2 | 0% | 460634 |

Per-scenario p95 and thresholds: `raw/stage-0/benchmark.log`.

## Checks before trusting the numbers

- A non-zero *failed* column usually means 429/403 from the rate limiter or connection-pool timeouts, not slowness. Look at `raw/stage-0/*.log` and the server log.
- Run on an idle machine, with the load generator and the server not competing for the same cores if possible.
- Compare only runs with the same duration, sweep levels and hardware.
