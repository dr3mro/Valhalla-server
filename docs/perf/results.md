# Performance results by stage

Baseline = original code. Stage N = plan step N+1 (Stage 0 = Step 1, Stage 1 = Step 2, Stage 2 = Step 3, Stage 3 = Step 4 query binding).
Stage 1 was run with `--reset-db` (empty tables); baseline and Stage 0 were not, so compare with care.
Raw numbers: [results.csv](results.csv). Charts: https://claude.ai/artifact/9jFisH1s5VsqJeMju3NTHv

| Metric | Unit | Baseline | Stage 0 | Stage 1 | Stage 2 | Stage 3 |
|---|---|---|---|---|---|---|
| Mixed throughput | req/s | 4004.2 | 5114.9 | 5456.7 | 7954.1 | 13372.7 |
| Read sweep, 1 user | req/s | 5311.6 | 5363.3 | 5299.3 | 5356.2 | 7873.0 |
| Read sweep, 8 users | req/s | 14373.0 | 14790.5 | 18205.2 | 17839.5 | 23129.0 |
| Read sweep, 32 users | req/s | 13607.0 | 13800.6 | 19209.3 | 19220.7 | 25824.6 |
| Read sweep, 128 users | req/s | 12932.4 | 13389.1 | 19327.7 | 16498.4 | 25604.1 |
| Mixed p95 latency | ms | 32.4 | 23.6 | 28.4 | 19.9 | 11.9 |
| Mixed median latency | ms | 17.2 | 13.8 | 11.8 | 8.0 | 4.7 |
| Mixed p99 latency | ms | 47.2 | 34.2 | 41.5 | 28.2 | 16.9 |
| Crud p95 latency | ms | 28.25 | 20.42 | 28.13 | 21.51 | 9.68 |
| Reads p95 latency (mixed run) | ms | 33.09 | 24.24 | 28.45 | 19.58 | 12.32 |
| Read sweep p95, 128 users | ms | 11.6 | 11.2 | 11.8 | 13.7 | 8.8 |
