# Performance run: stage-2

- Date: 2026-11-15T08:15:33Z
- Git: dc5037d-dirty
- Host: Darwin 27.2.0 arm64, 10 CPUs
- Target: http://127.0.0.1:8080 (existing server)
- Sweep: 20s per level, endpoint POST /clients/providers/read (auth + permission check + 1 DB read)

## Concurrency sweep

| VUs | req/s | median ms | p95 ms | p99 ms | failed | requests |
|-----|-------|-----------|--------|--------|--------|----------|
| 1 | 5356.2 | 0.2 | 0.2 | 0.2 | 0% | 107162 |
| 8 | 17839.5 | 0.4 | 0.6 | 0.7 | 0% | 356854 |
| 32 | 19220.7 | 1.6 | 2.9 | 3.6 | 0% | 384496 |
| 128 | 16498.4 | 7 | 13.7 | 16.9 | 0% | 330136 |

## Mixed benchmark (reads + crud + auth)

| scenario | req/s | median ms | p95 ms | p99 ms | failed | requests |
|----------|-------|-----------|--------|--------|--------|----------|
| all | 7954.1 | 8 | 19.9 | 28.2 | 0% | 716243 |

Per-scenario p95 and thresholds: `raw/stage-2/benchmark.log`.

## Checks before trusting the numbers

- A non-zero *failed* column usually means 429/403 from the rate limiter or connection-pool timeouts, not slowness. Look at `raw/stage-2/*.log` and the server log.
- Run on an idle machine, with the load generator and the server not competing for the same cores if possible.
- Compare only runs with the same duration, sweep levels and hardware.
