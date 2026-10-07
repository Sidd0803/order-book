# Performance log (Phase 3)

Rules: Release builds only, change one thing at a time, report relative
speedups. Absolute numbers on a laptop are noisy; run each config 3+ times
and note the spread.

| # | Experiment | Build | N orders | total time | p50 | p99 | vs baseline | notes |
|---|------------|-------|----------|-----------:|----:|----:|------------:|-------|
| 0 | baseline: map + list | Release | | | | | 1.00x | |
