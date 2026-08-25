# Results

All numbers below are simulator output. Reproduce with
`./experiments/reproduce_core.sh --mode core` (Exp1) or `--mode full` (Exp2–4).
Statistical treatment and metric definitions: `docs/methodology.md`.
What the invariants do and do not guarantee: `docs/validation.md`.

---

## Experiment 1 — GC policy comparison (5 seeds)

Workload `bursty_write`: uniform-random writes over the full logical capacity at 3×
overwrite pressure with periodic ~20 ms idle gaps. 2.7 M requests, OP 14 %,
queue depth 16, seeds 0–4.

**Absolute (mean ± 1σ over 5 seeds)**

| Policy | p99 latency (s) | WAF | GC-induced stall (s) |
|---|---:|---:|---:|
| foreground | 582 ± 13 | 1.912 ± 0.002 | 1649 ± 4 |
| fixed_background | 14,640 ± 575 | 33.32 ± 0.31 | 0 |
| **queue_aware** | **556 ± 4** | 1.991 ± 0.003 | **1435 ± 5** |

**Paired per-seed deltas (queue_aware vs foreground, same trace)**

| Metric | mean | σ | per-seed |
|---|---:|---:|---|
| p99 latency | −4.35 % | 2.10 % | −5.07, −2.35, −4.56, −7.38, −2.40 |
| GC-induced stall | −12.95 % | 0.23 % | −12.91, −12.59, −13.04, −13.03, −13.20 |
| WAF | +4.15 % | 0.10 % | +4.12, +3.98, +4.17, +4.23, +4.24 |

Raw `results/raw/exp1_bursty_multiseed.csv` · summary
`results/processed/exp1_multiseed_summary.csv` · manifest
`results/manifests/exp1_multiseed.json` · figures `fig1`–`fig3` (bars are means,
error bars ±1σ).

### What this supports

Queue-aware GC lowers p99 on **every seed** and cuts host-blocking GC stall by ~13 %
with a σ of 0.23 %, for a steady ~4 % WAF cost with a σ of 0.10 %. The mechanism is
exactly what the design predicts: it converts stalls the foreground policy pays at the
critical threshold into work done during idle windows, and pays for it in pages copied
that would have been invalidated anyway.

### What this does not support

**"Queue-aware GC is 5 % faster at p99."** The per-seed p99 delta ranges from −2.4 % to
−7.4 % with σ ≈ half the mean. The direction is solid; the magnitude is not resolved by
5 seeds and is workload-dependent. The stall and WAF deltas are the tight ones and are
the numbers to quote.

### Why fixed_background loses so badly

It triggers background GC on free-ratio alone, with no queue gate. Under sustained
write pressure it GCs far more often than necessary, each round copying valid pages
that would have been invalidated shortly after — runaway self-inflicted amplification
(WAF 33.3 vs 1.91).

Its `gc_induced_stall_ns = 0` is **not a win**. That counter measures only *forced,
host-blocking* GC; a policy that does all its GC in the background scores 0 by
construction while being 25× worse on the metric the host actually feels. This is a
metric artifact, and it is the reason results are reported as latency-and-WAF pairs
rather than by any single counter.

---

## Experiment 2 — Queue depth (single seed)

Mixed 70R/30W, queue-aware policy, `--qd 1|4|16|32`.

| Queue depth | Throughput (req/s) | p99 latency (ns) |
|---:|---:|---:|
| 1 | 4,639 | 4.27e10 |
| 4 | 12,520 | 1.58e10 |
| 16 | 12,750 | 1.55e10 |
| 32 | 12,750 | 1.55e10 |

Throughput saturates at queue depth 4, which is exactly the channel count in
`configs/nand.conf`. Past that point the channels, not admission, are the bottleneck —
deeper queues add queueing without adding parallelism. Raw
`results/processed/exp2_queue_depth_sweep.csv`, figures `fig4`, `fig5`.

Reported as a **saturation point**, not as an effect size: single seed.

---

## Experiment 3 — Over-provisioning (single seed)

Random write at 3× overwrite pressure, `--op 7|14|28`.

| OP | WAF | GC count | p99 latency (ns) |
|---:|---:|---:|---:|
| 7 % | 2.63 | 25,967 | 2.08e12 |
| 14 % | 1.91 | 16,260 | 1.40e12 |
| 28 % | 1.35 | 7,989 | 6.98e11 |

Monotonic and textbook: more spare physical capacity means each GC round finds blocks
with fewer valid pages, so fewer rounds each copy less. Raw
`results/processed/exp3_op_sweep.csv`, figures `fig6`, `fig7` (WAF–p99 trade-off).

**Methodological note:** the first attempt at this sweep reused one trace across all
three OP levels. That is wrong — OP changes how much physical capacity is reserved, not
which LBAs are valid, so the same trace does not represent the same workload at
different OP. Caught before publication and rerun with per-OP traces. The numbers above
are from the corrected runs.

---

## Experiment 4 — Workload locality (single seed)

| Workload | WAF | Pages moved by GC | p99 latency (ns) |
|---|---:|---:|---:|
| uniform_random (pure write) | 1.91 | 2,458,531 | 1.40e12 |
| hot_cold (50/50 R/W) | 1.02 | 29,329 | 2.41e11 |

Locality reduces GC pressure enormously — hot pages are re-invalidated before GC ever
reaches them, so victim blocks are nearly all invalid.

**This comparison is not matched and should not be quoted as a locality effect size.**
`hot_cold` is 50/50 read/write by construction while the uniform baseline is pure
write, so roughly half the gap is simply fewer writes. Isolating locality would need a
hot/cold *write-only* pattern against uniform write-only. Not run; recorded in
`docs/limitations.md` rather than papered over. Raw
`results/processed/exp4_locality.csv`.

---

## Summary of what is and is not established

| Finding | Strength |
|---|---|
| Queue-aware GC reduces host-blocking GC stall ~13 % vs foreground | **Strong** — 5 seeds, σ 0.23 % |
| It costs ~4 % WAF | **Strong** — 5 seeds, σ 0.10 % |
| It lowers p99 | **Directionally strong** (5/5 seeds), magnitude unresolved (2.4–7.4 %) |
| Fixed-threshold background GC is far worse than either | **Strong** — 5 seeds, order-of-magnitude gap |
| Throughput saturates at queue depth = channel count | Single seed, mechanism-consistent |
| WAF decreases monotonically with OP | Single seed, mechanism-consistent |
| Locality reduces GC pressure | Single seed, **confounded** with R/W mix |
| Anything about physical SSD performance | **Not established** — no device was measured |
