# Performance run: stage-6

- Date: 2026-11-15T09:53:31Z
- Git: 33ef8fd-dirty
- Host: Darwin 27.2.0 arm64, 10 CPUs
- Target: http://127.0.0.1:8080 (existing server)
- Sweep: 20s per level, endpoint POST /clients/providers/read (auth + permission check + 1 DB read)

## Concurrency sweep

| VUs | req/s | median ms | p95 ms | p99 ms | failed | requests |
|-----|-------|-----------|--------|--------|--------|----------|
| 1 | 7305.5 | 0.1 | 0.1 | 0.2 | 0% | 146230 |
| 8 | 21067.8 | 0.3 | 0.5 | 0.6 | 0% | 421627 |
| 32 | 22883.3 | 1.3 | 1.9 | 2.4 | 0% | 458006 |
| 128 | 18477.3 | 6.6 | 9 | 10.5 | 0% | 369941 |

## Mixed benchmark (reads + crud + auth)

| scenario | req/s | median ms | p95 ms | p99 ms | failed | requests |
|----------|-------|-----------|--------|--------|--------|----------|
| all | 9091 | 8.3 | 14.1 | 17.1 | 0% | 819173 |

Per-scenario p95 and thresholds: `raw/stage-6/benchmark.log`.

## Login burst (read latency with and without simultaneous logins)

| phase | read req/s | read p95 ms | read p99 ms | login req/s |
|-------|-----------|-------------|-------------|-------------|
| reads alone | 5452.9 | 0.9 | 1.2 | - |
| reads during login burst | 4610.6 | 1.3 | 2 | 25.1 |

## Checks before trusting the numbers

- A non-zero *failed* column usually means 429/403 from the rate limiter or connection-pool timeouts, not slowness. Look at `raw/stage-6/*.log` and the server log.
- Run on an idle machine, with the load generator and the server not competing for the same cores if possible.
- Compare only runs with the same duration, sweep levels and hardware.
