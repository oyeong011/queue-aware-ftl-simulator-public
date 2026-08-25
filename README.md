# Queue-Aware FTL Simulator

> **Public code snapshot.** Everything needed to build, test, and reproduce the results
> is here. The six workload trace CSVs (~390 MB) are **not** tracked: they are fully
> determined by the seeds and arguments in `results/manifests/exp1_multiseed.json`, and
> `./experiments/reproduce_core.sh --mode core` regenerates them and reproduces the
> committed result CSVs byte-for-byte. The development repository, which tracks the
> traces and the full commit history, is private.

## Problem

> Host I/O queue 상태를 활용한 background garbage collection이 write amplification을
> 과도하게 증가시키지 않으면서 GC로 인한 tail latency를 낮출 수 있는가?

## What was implemented

A deterministic, page-level FTL + NAND block simulator in C++17 (`include/ftlsim`,
`src/Ftl.cc`): L2P mapping, per-channel free-block allocation, block-level erase/GC,
and three GC policies — foreground-greedy, fixed-threshold background, and
queue-aware background (defers background GC unless outstanding I/O is low).
Timing model: per-channel busy-until tracking with a bounded admission queue
(`queue_depth`), so both device service time and host queueing show up in latency.

## Key verified result — 5 seeds, mean ± 1σ

Random-write workload with 3× logical-capacity overwrite pressure and periodic
20 ms idle gaps (`workloads/generator.py --pattern bursty_write`), `configs/nand.conf`
+ each of `configs/ftl_{foreground,fixed_background,queue_aware}.conf`, seeds 0–4:

| Policy | p99 latency (s) | WAF | GC-induced stall (s) |
|---|---:|---:|---:|
| foreground | 582 ± 13 | 1.912 ± 0.002 | 1649 ± 4 |
| fixed_background | 14,640 ± 575 (25× worse) | 33.32 ± 0.31 | 0 (all GC counted as background) |
| **queue_aware** | **556 ± 4** | 1.991 ± 0.003 | **1435 ± 5** |

Paired per-seed deltas, queue-aware vs foreground (same trace, both policies):

| Metric | mean | σ | per-seed |
|---|---:|---:|---|
| p99 latency | **−4.35 %** | 2.10 % | −5.07, −2.35, −4.56, −7.38, −2.40 |
| GC-induced stall | **−12.95 %** | 0.23 % | −12.91, −12.59, −13.04, −13.03, −13.20 |
| WAF (cost) | **+4.15 %** | 0.10 % | +4.12, +3.98, +4.17, +4.23, +4.24 |

Raw: `results/raw/exp1_bursty_multiseed.csv` → `results/processed/exp1_multiseed_summary.csv`.
Manifest (generator args, seeds, config sha256s): `results/manifests/exp1_multiseed.json`.
Figures: `figures/fig1_policy_p99_latency.png`, `fig2_policy_waf.png`, `fig3_policy_gc_stall.png`
(bars are means, error bars ±1σ over 5 seeds).

**Reading it honestly**: the direction is consistent — queue-aware lowers p99 on all
5 seeds and cuts host-blocking GC stall by ~13 % with very little spread, paying a
steady ~4 % WAF because it also GCs opportunistically during idle windows. But the
p99 effect size is not tight: 2.4 %–7.4 % across seeds, σ ≈ half the mean. The
defensible claim is **"consistently lower p99, magnitude workload-dependent"**, not
"5 % faster". The stall reduction (σ = 0.23 %) and the WAF cost (σ = 0.10 %) are the
robust numbers.

Fixed-threshold background GC is dramatically worse on every axis — it GCs on a fixed
schedule regardless of queue state, causing runaway self-inflicted amplification under
this workload. Note its 0 stall is an artifact of the metric definition
(`gc_induced_stall_ns` counts only host-blocking forced GC), not a good result — see
`docs/methodology.md`.

This is one workload pattern and one NAND geometry, swept over seeds only — see
`docs/limitations.md`.

## Why fixed_background performs so badly

`ftl_fixed_background.conf` triggers background GC purely on free-ratio threshold,
with no queue-state gate. Under sustained write pressure it ends up GC'ing more
aggressively and more often than necessary, each round moving valid pages that
would otherwise have been invalidated soon anyway — a real, reproducible
"aggressive background GC backfires" result, not a bug (see git history: an earlier
version of `checkAndRunGc` *did* have a real bug — global rather than per-channel
free-ratio accounting caused a crash — fixed in commit history, unrelated to this
policy comparison).

## Experiment 2 — queue depth sweep

Mixed 70R/30W workload, `configs/ftl_queue_aware.conf`, `--qd 1|4|16|32`:

| Queue depth | Throughput (req/s) | p99 latency (ns) |
|---:|---:|---:|
| 1 | 4,639 | 4.27e10 |
| 4 | 12,520 | 1.58e10 |
| 16 | 12,750 | 1.55e10 |
| 32 | 12,750 | 1.55e10 |

Throughput saturates at queue_depth=4, matching `configs/nand.conf`'s 4 channels —
once depth ≥ channel count, deeper queues stop helping because the channels
themselves are the bottleneck, not admission. Raw:
`results/processed/exp2_queue_depth_sweep.csv`. Figures: `fig4_qd_throughput.png`,
`fig5_qd_p99_latency.png`.

## Experiment 3 — over-provisioning sweep

Random write, 3x logical-capacity overwrite pressure, `--op 7|14|28`:

| OP | WAF | GC count | p99 latency (ns) |
|---:|---:|---:|---:|
| 7% | 2.63 | 25,967 | 2.08e12 |
| 14% | 1.91 | 16,260 | 1.40e12 |
| 28% | 1.35 | 7,989 | 6.98e11 |

Monotonic, textbook OP-vs-WAF relationship — more spare physical capacity means
fewer, less painful GC cycles. Raw: `results/processed/exp3_op_sweep.csv`.
Figures: `fig6_op_waf.png`, `fig7_waf_p99_tradeoff.png`.

## Experiment 4 — workload locality

Uniform-random vs. hot/cold (90% of accesses hit the hottest 10% of the working
set; hot_cold is a 50/50 R/W pattern by construction, uniform_random here is
pure write — not a fully matched comparison, see caveat below), OP=14%:

| Workload | WAF | pages moved by GC | p99 latency (ns) |
|---|---:|---:|---:|
| uniform_random (pure write) | 1.91 | 2,458,531 | 1.40e12 |
| hot_cold (50/50 R/W) | 1.02 | 29,329 | 2.41e11 |

Locality reduces GC pressure dramatically — but part of this gap is the mixed
read fraction in hot_cold halving the write count, not locality alone. Raw:
`results/processed/exp4_locality.csv`. No dedicated figure yet (would need a
matched-write-count rerun to isolate locality from R/W mix — see limitations.md).

## Reproduction

One command, three depths:

```bash
./experiments/reproduce_core.sh --mode smoke   # ~4 s     build + 12 invariants + one real run
./experiments/reproduce_core.sh --mode core    # ~1.5 min + Exp1 across 5 seeds + figures
./experiments/reproduce_core.sh --mode full    # + Exp2/3/4 sweeps (timing not yet measured)
```

Each mode does a clean rebuild, runs `ctest`, prints the binary hash and git commit,
regenerates traces from their seeds into `$FTLSIM_SCRATCH` (default `/tmp/ftlsim-traces`),
and writes raw CSVs + a manifest. Measured on this host (Ubuntu 24.04, g++ 13.3):
**smoke 3.6 s, core 1.2 min** (each seed's generate-plus-three-policy cycle takes ~12 s).
`full` has not been timed end-to-end and no estimate is quoted for it.

`--mode core` was run from a cleared scratch directory and reproduced
`results/raw/exp1_bursty_multiseed.csv` and
`results/processed/exp1_multiseed_summary.csv` **byte-for-byte** against the committed
copies — so trace generation is stable across runs, not just the simulator.

Manual equivalent of the headline experiment:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
ctest --test-dir build --output-on-failure      # 12 invariants
./experiments/run_exp1_seeds.sh                 # seeds 0-4 x 3 policies
python3 analysis/aggregate_seeds.py             # summary CSV + figures with error bars
```

## Documentation

| File | Contents |
|---|---|
| `docs/architecture.md` | data path + request lifecycle + experiment workflow diagrams, component table |
| `docs/methodology.md` | research question, variables, the three policies as pseudocode, metric definitions, statistical treatment |
| `docs/validation.md` | the 12 invariants, what each test checks, the bug these checks caught |
| `docs/results.md` | every experiment’s numbers, what each supports, and what it does not |
| `docs/limitations.md` | what this model does not do, and which numbers should not be trusted how far |

## Limitations

See `docs/limitations.md`. Headline: all four experiments have run with real data, but
only Experiment 1 is swept across seeds — Experiments 2–4 are single-seed directional
results. 7 of 8 planned figures exist (the two time-series figures need per-event
logging the simulator does not emit). No wear leveling, ECC, bad-block handling, or
power-loss recovery. **Every number here is simulator output; this repository contains
no measurement of a physical NVMe or SATA device.**
