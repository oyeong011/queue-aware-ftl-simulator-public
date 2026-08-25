# Validation

Everything here runs from one command:

```bash
ctest --test-dir build --output-on-failure
```

`tests/test_invariants.cc` is a single assert-style binary. Its `CHECK` macro
**counts** failures rather than aborting, so one broken invariant does not hide the
other eleven — the binary always runs every scenario and exits non-zero if any failed.

## Structural invariants — `Ftl::validateInvariants()`

Called after every test scenario. It re-derives all bookkeeping from `page_state_` and
compares against the incrementally maintained counters, so any drift fails loudly:

| # | Invariant | How it is checked |
|---|---|---|
| 1 | One LPN never maps to two valid physical pages | `l2p_[lpn] == p` implies `p2l_[p] == lpn`, checked for every mapped LPN |
| 2 | An invalid page is never counted as valid | every mapped LPN's target must be `VALID`; `mapped_count == valid_count` catches dangling valid pages |
| 3 | free + valid + invalid == total pages | recomputed by scanning `page_state_` |
| 3b | per-channel free counts sum to the global free count | separate check — the bug in §"Bug found" below was exactly this drifting |
| 3c | per-block valid/invalid counters sum to the global totals | block counters drive GC victim selection, so they get their own check |

## Behavioural invariants — one test function each

| # | Invariant | Test |
|---|---|---|
| 4 | Overwrite invalidates the old page (LPN stays mapped, old PPN becomes INVALID) | `test_overwrite_invalidates_old_page` |
| 5 | GC preserves valid data — an untouched LPN is still correctly mapped after heavy GC | `test_gc_preserves_valid_data` |
| 6 | Full sequential write maps every logical page exactly once, `host_write_pages == capacity` | `test_full_sequential_write_mapping_count` |
| 7 | WAF ≈ 1.0 when no overwrite and no GC copy has occurred | `test_waf_near_one_without_gc` (asserts 0.999 < WAF < 1.001) |
| 8 | Under GC pressure, `nand_write_pages >= host_write_pages`, and GC actually triggered | `test_gc_nand_writes_gte_host_writes` |
| 9 | Same seed + same config reproduces bit-identical metrics | `test_deterministic_reproducibility` (WAF, gc_count, latency all compared for exact equality across two runs) |
| 10 | `queue_depth` bounds outstanding requests — qd=1 shows strictly higher queue wait than a deep queue | `test_queue_depth_limits_outstanding` |
| 11 | No deadlock at critical free-page pressure — the run completes rather than hanging | `test_no_deadlock_under_critical_pressure` |
| 12 | An out-of-range LBA fails explicitly (`InvalidLbaError`), not silently | `test_invalid_lba_fails_explicitly` |

Invariant 9 is the one that makes every published number checkable: any result in
`results/` can be regenerated exactly from its manifest, and a mismatch means something
changed in the simulator, not in the run.

Invariant 11 exists because a naive "GC until above threshold" loop can spin forever
when no reclaimable victim exists. The implementation bounds the loop and raises
`NAND full` when a channel genuinely has no reclaimable block, rather than hanging.

## Bug found by these checks

The first Experiment 1 run crashed with `NAND full`. Root cause: `checkAndRunGc`
compared a **global** free-page ratio against the threshold, while free blocks are
strictly partitioned per channel. One channel could be fully exhausted while the global
ratio still looked healthy, so GC was never triggered for it and its next allocation
failed.

The fix was to track `free_pages_per_channel_` and make both the critical and background
gates per-channel — a change in the one shared function all three policies route
through, not a guard added at the crash site. The per-channel-sum invariant (3b above)
was added at the same time so the same class of drift fails in `ctest` rather than in an
experiment.

This is recorded because it changes how the results should be read: the numbers in
README.md come from the post-fix simulator, and no pre-fix numbers are quoted anywhere.

## What is *not* validated

- **No comparison against a real device or against another simulator.** There is no
  ground-truth WAF or latency to validate against, so the invariants check *internal
  consistency*, not physical accuracy. A result that is internally consistent can still
  be a poor model of real NAND.
- **NAND timing parameters are nominal**, taken as representative consumer-TLC
  order-of-magnitude values, not measured from a device. Absolute latencies are
  therefore not meaningful; only relative comparisons under identical parameters are.
- **No test asserts that queue-aware GC is better.** The tests verify the simulator is
  self-consistent; whether the policy wins is an experimental result that is allowed to
  come out negative.
