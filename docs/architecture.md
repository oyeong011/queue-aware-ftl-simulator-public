# Architecture

## Data path being modeled

```mermaid
flowchart TD
    W[HostWorkload<br/>trace CSV: arrival_ns, op, lba, size] --> Q
    Q[HostQueue<br/>bounded by queue_depth<br/>records queue wait] --> F

    subgraph F[FTL]
      L2P[L2P map<br/>LPN to PPN]
      P2L[P2L / page-state table<br/>FREE / VALID / INVALID]
      ALLOC[Free-page allocator<br/>per-channel write frontier]
      INV[Out-of-place update<br/>+ old-page invalidation]
      GC[GC policy<br/>foreground / fixed-bg / queue-aware]
    end

    F --> N
    subgraph N[NANDModel]
      CH[channel 0..C-1<br/>busy-until clock each]
      DIE[dies per channel]
      BLK[blocks: valid/invalid counts, erase count]
      PG[pages: t_read / t_program / t_erase]
    end

    N --> M[MetricsCollector<br/>latency percentiles, WAF,<br/>GC counts, stall, erase stats]
    GC -. victim select, valid-page copy-forward,<br/>block erase .-> N
    GC -. reads outstanding_io .-> Q
```

The one link that makes this project what it is: **`GC` reads `outstanding_io` from
`HostQueue`**. Foreground and fixed-background policies do not — they decide purely on
free-page ratio. The queue-aware policy adds queue occupancy as a second input, which is
the entire hypothesis under test.

## Request lifecycle

```mermaid
sequenceDiagram
    participant T as Trace
    participant Q as HostQueue
    participant F as FTL
    participant G as GC
    participant N as NAND channel

    T->>Q: request arrives at arrival_ns
    Q->>Q: admit if outstanding < queue_depth,<br/>else wait (queue wait time accrues)
    Q->>F: dispatch
    F->>G: checkAndRunGc(free_ratio, outstanding_io)
    alt free_ratio <= critical
        G->>N: FORCED GC — host request stalls behind it<br/>(counts as foreground_gc + gc_induced_stall)
    else free_ratio <= background AND outstanding_io <= low_queue_threshold
        G->>N: opportunistic background GC<br/>(counts as background_gc)
    else
        G-->>F: defer
    end
    F->>F: WRITE — allocate free page, remap LPN,<br/>invalidate old PPN
    F->>N: program / read page on channel(PPN)
    N-->>F: completion at max(channel_busy_until, dispatch) + t_op
    F-->>Q: retire, record latency = completion - arrival
```

## Experiment workflow

```mermaid
flowchart LR
    C[configs/*.conf<br/>nand geometry + FTL params] --> S
    G[workloads/generator.py<br/>pattern + seed] --> TR[trace CSV<br/>deterministic from seed]
    TR --> S[build/ftlsim_cli]
    S --> RAW[results/raw/*.csv<br/>one metric row per run]
    RAW --> AGG[analysis/aggregate_seeds.py<br/>analysis/plot_results.py]
    AGG --> PROC[results/processed/*.csv]
    AGG --> FIG[figures/*.png]
    S --> MAN[results/manifests/*.json<br/>seeds, args, config sha256]
    T[tests/test_invariants.cc<br/>ctest] -.gates.-> S
```

## Component responsibilities

| Component | File | Holds |
|---|---|---|
| `NandGeometry` | `include/ftlsim/Config.h` | page size, pages/block, blocks/die, dies/channel, channels, t_read/t_program/t_erase |
| `FtlParams` | `include/ftlsim/Config.h` | OP %, critical/background free ratios, low-queue threshold, GC policy, queue depth |
| `Ftl` | `src/Ftl.cc` | L2P/P2L, page state, per-channel free counts, write frontier, GC, event loop |
| `RunMetrics` | `include/ftlsim/Metrics.h` | latency samples → p50/p95/p99, WAF, GC counters, erase stats |
| `Trace` | `include/ftlsim/Trace.h` | CSV parse, LBA→LPN, size→page count |

`Ftl::run()` is a single-pass discrete-event loop over the arrival-ordered trace; there
is no separate event heap. Per-channel `busy_until` clocks provide the parallelism, and
each request's completion is `max(channel_busy_until, admit_time) + service_time`. This
is a deliberate simplification — see `docs/limitations.md` for what it costs.

## Address mapping

Page-level (not block- or hybrid-level) mapping:

- `l2p_[lpn] -> ppn` (`-1` = unmapped)
- `p2l_[ppn] -> lpn` (`-1` = not holding valid data)
- `page_state_[ppn] ∈ {FREE, VALID, INVALID}`

Every write allocates a fresh FREE page from the current channel's write frontier,
points `l2p_` at it, and marks the previous PPN INVALID. Blocks track `valid_count` /
`invalid_count` so GC victim selection is O(blocks) greedy-min-valid without rescanning
pages. `Ftl::validateInvariants()` re-derives all of these counts from
`page_state_` and fails if any bookkeeping has drifted — it is called from the test
suite after every scenario.
