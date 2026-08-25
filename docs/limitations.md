# Limitations

- **All 4 planned experiments now run** (policy comparison, queue depth sweep,
  OP sweep, workload locality). Each is a single seed/config point, not a
  multi-seed statistical sweep — see below.
- **7 of 8 planned figures exist.** Missing: a locality-specific figure
  (Experiment 4's uniform-vs-hot/cold comparison isn't plotted, and the two
  time-series figures — free-page-ratio-over-time and queue-occupancy/GC-event
  timeline — need per-event logging the simulator doesn't currently emit;
  `RunMetrics` only holds final/aggregate values, not a trace of intermediate
  states).
- **Experiment 4's uniform-random vs. hot/cold comparison isn't a matched
  A/B.** hot_cold is a 50/50 read/write pattern by construction while the
  uniform_random baseline used is pure write, so roughly half the WAF/GC gap
  is the reduced write count, not locality alone. A clean isolation would
  need a hot/cold *write-only* pattern compared against uniform *write-only*.
- **One workload pattern, one NAND geometry — swept over seeds only.** The
  headline queue-aware result is now run across 5 seeds (mean ± 1σ, paired
  per-seed deltas in README.md), so run-to-run variance *is* characterized.
  What is still a single point: the workload pattern (bursty random write,
  3× overwrite pressure, 20 ms idle gaps every 50 writes), the NAND geometry,
  and the threshold settings. The p99 effect size varies 2.4 %–7.4 % across
  seeds (σ ≈ half the mean), so the magnitude should not be quoted as a
  single number — only the direction and the much tighter stall (−12.95 %,
  σ 0.23 %) and WAF (+4.15 %, σ 0.10 %) deltas are stable.
- **Experiments 2–4 remain single-seed.** They are reported as directional
  findings (throughput saturates at channel count; WAF falls monotonically
  with OP), not as effect sizes, and have no variance estimate.
- **The idle gaps are load-bearing.** `bursty_write` injects ~20 ms idle
  windows; without them a queue-aware policy has no idle window to exploit
  and cannot differ from foreground by construction. The result is a
  statement about bursty workloads, not about sustained-load SSDs.
- **No wear leveling, ECC, bad-block handling, or power-loss recovery.**
  Erase-count stats are tracked (mean/stddev/max) but nothing acts on them.
- **Single-request-granularity timing.** A multi-page request is billed as
  one device-time block on one channel rather than modeled as independent
  per-page sub-events; this slightly understates cross-request pipelining
  within a single large request.
- **GC victim selection is per-channel greedy-min-valid only.** No hybrid
  or cost-benefit victim selection, no static/dynamic wear-leveling bias.
- **`stream_id` in the trace schema is parsed but unused** — hot/cold
  stream-aware placement is not implemented; the `hot_cold` workload pattern
  exists in the generator but the FTL doesn't yet treat streams specially
  beyond what LBA locality alone produces.
- **Known bug found and fixed this session**: `checkAndRunGc` originally
  checked a *global* free-page ratio while free blocks are strictly
  partitioned per channel, which could crash with "NAND full" on one channel
  while the global ratio still looked healthy. Fixed by tracking
  `free_pages_per_channel_` and gating GC decisions per-channel (see git log).
  Recorded here as an example of what surfaced under real experimentation,
  not swept under the rug.
