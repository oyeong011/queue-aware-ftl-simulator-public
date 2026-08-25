# Methodology

## Research question

> Can background garbage collection that is aware of host I/O queue state reduce
> GC-induced tail latency without inflating write amplification unacceptably?

This is a trade-off question, not a "does it go faster" question, so every result is
reported as a **pair**: a tail-latency number and a WAF number. A policy that lowers p99
by paying arbitrary WAF has not answered the question.

## Independent variables

| Variable | Values swept | Where |
|---|---|---|
| GC policy | foreground / fixed_background / queue_aware | Exp 1 |
| Queue depth | 1, 4, 16, 32 | Exp 2 |
| Over-provisioning | 7%, 14%, 28% | Exp 3 |
| Workload locality | uniform random / hot-cold | Exp 4 |
| Seed | 0–4 | Exp 1 |

Held fixed unless swept: NAND geometry (`configs/nand.conf` — 4 KiB page, 256
pages/block, 512 blocks/die, 2 dies/channel, 4 channels; t_read 50 µs, t_program 600 µs,
t_erase 3 ms), OP 14%, queue depth 16, `critical_free_ratio` 0.05,
`background_free_ratio` 0.20, `low_queue_threshold` 2.

The geometry is a scaled-down consumer TLC SSD: the ratios (read:program:erase, pages
per block, channel count) are realistic, the absolute capacity is not — it is sized so a
full sweep finishes in seconds. Conclusions about *ordering and direction* transfer;
absolute latency numbers do not, and are never quoted as SSD performance.

## The three policies

All three share the same critical-threshold safety valve. They differ only in when
*background* GC is allowed to run.

```
# every policy, on every write dispatch to a channel:
while free_pages(channel) == 0 or free_ratio(channel) <= critical_threshold:
    force GC now                      # host request stalls behind it
                                      # -> foreground_gc_count, gc_induced_stall_ns

# then, policy-specific:
if free_ratio(channel) <= background_threshold:
    A foreground:        never schedule background GC
    B fixed_background:  always schedule background GC
    C queue_aware:       schedule background GC only if outstanding_io <= low_queue_threshold
```

Policy C is the hypothesis: it should convert stalls that A pays at the critical
threshold into work done during idle windows, at the cost of some GC that A would never
have needed (pages it moved may have been invalidated soon anyway) — i.e. p99 down, WAF
up. Policy B is the control that shows what happens when you do background GC *without*
the queue gate.

The free-ratio check is **per channel**, not global. Free blocks never migrate between
channels, so a global ratio can look healthy while one channel is exhausted. Getting
this wrong caused a real "NAND full" crash during the first experiment run; see
`docs/validation.md`.

## Workloads

Traces are CSV (`workloads/schema.md`): `arrival_ns,op,lba,size_bytes,stream_id`.
`workloads/generator.py` produces them from `(pattern, capacity_pages, count,
working_set_frac, qd, seed)` — fully deterministic, so a ~90 MB trace is reproducible
from six values instead of stored.

The headline workload is `bursty_write`: uniform-random writes over the full logical
capacity at 3× overwrite pressure, with periodic ~20 ms idle gaps. The idle gaps matter
— without them there is no idle window for a queue-aware policy to exploit, and the
experiment cannot distinguish C from A by construction. This is stated up front because
it is the workload assumption the whole result rests on.

## Metrics and how they are computed

- **Latency** = completion − arrival, per request, including queue wait. Percentiles
  (p50/p95/p99) are computed by sorting the full sample vector, not from a histogram.
- **WAF** = `nand_write_pages / host_write_pages`. `host_write_pages` counts pages
  written on behalf of trace requests; `nand_write_pages` counts those plus every valid
  page copied forward by GC. Erases are not counted (they free pages, they do not write
  them). WAF = 1.0 exactly when no GC copy has occurred.
- **`gc_induced_stall_ns`** = time spent in *forced* (critical-threshold) GC only, i.e.
  GC the host waited on. Background GC contributes 0 to this metric by definition —
  which is why `fixed_background` reports 0 stall while being the worst policy overall.
  The metric measures "how much did GC block the host", not "how much GC ran".
- **Erase stats** (mean/stdev/max per block) are recorded but nothing acts on them; no
  wear leveling is implemented.

## Statistical treatment

Experiment 1 is run across 5 seeds and reported as **mean ± 1σ with the per-seed paired
deltas listed**. A single-seed 5% difference is not distinguishable from run-to-run
variation, and the whole project rests on that one comparison, so it gets the seeds.
Paired per-seed deltas (same trace, three policies) are reported alongside the means
because the seed-to-seed variation in absolute p99 is larger than the effect being
measured — the paired difference is the meaningful quantity.

Experiments 2–4 are single-seed sweeps. They are reported as directional/monotonic
findings (throughput saturates at channel count; WAF falls monotonically with OP), not
as precise effect sizes, and that limitation is stated where the numbers appear.

## What gates a result

No number is published unless:

1. `ctest --test-dir build --output-on-failure` passes (12 invariants, see
   `docs/validation.md`);
2. the run's raw CSV row is in `results/raw/`;
3. a manifest in `results/manifests/` records the generator args, seeds, config
   sha256s, and simulator commit.

Everything reported here is simulator output. There is no physical NVMe device in this
setup, and no number in this repository is a measurement of real SSD hardware.
