# Performance run: baseline

- Date: 2026-11-15T06:49:51Z
- Git: 15da3b3-dirty
- Host: Darwin 27.2.0 arm64, 10 CPUs
- Target: http://127.0.0.1:18080 (fresh container container, default settings)
- Sweep: 5s per level, endpoint POST /clients/providers/read (auth + permission check + 1 DB read)
- **Smoke run: numbers are not meaningful**

## Concurrency sweep

| VUs | req/s | median ms | p95 ms | p99 ms | failed | requests |
|-----|-------|-----------|--------|--------|--------|----------|
| 1 | 2432.8 | 0.4 | 0.4 | 0.5 | 0% | 12196 |
| 8 | 4231.1 | 1.7 | 3.6 | 4.1 | 0% | 21182 |

## Mixed benchmark (reads + crud + auth)

| scenario | req/s | median ms | p95 ms | p99 ms | failed | requests |
|----------|-------|-----------|--------|--------|--------|----------|
| all | 1163.4 | 0.6 | 1.1 | 1.6 | 0.07% | 11681 |

Per-scenario p95 and thresholds: `raw/baseline/benchmark.log`.

## Checks before trusting the numbers

- A non-zero *failed* column usually means 429/403 from the rate limiter or connection-pool timeouts, not slowness. Look at `raw/baseline/*.log` and the server log.
- Run on an idle machine, with the load generator and the server not competing for the same cores if possible.
- Compare only runs with the same duration, sweep levels and hardware.
