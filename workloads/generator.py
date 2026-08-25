#!/usr/bin/env python3
"""Deterministic trace generator for the FTL simulator.

Output CSV schema (see workloads/schema.md):
    arrival_ns,op,lba,size_bytes,stream_id

Usage:
    python3 generator.py --pattern random_write --capacity-pages 100000 \
        --count 20000 --block-size 4096 --qd 16 --seed 0 --out trace.csv
"""
import argparse
import csv
import random
import sys

PATTERNS = [
    "seq_read", "seq_write", "rand_read", "rand_write",
    "mix_70r30w", "mix_90r10w", "hot_cold", "bursty_write",
]


def gen(pattern, capacity_pages, count, block_size_bytes, page_size_bytes,
        working_set_frac, qd, seed):
    rng = random.Random(seed)
    pages_per_req = block_size_bytes // page_size_bytes
    working_set = max(pages_per_req, int(capacity_pages * working_set_frac))
    # mean inter-arrival spacing so ~qd requests are in flight on average;
    # ponytail: no fancy renewal-process model, a fixed step is enough for a
    # deterministic reproducible trace.
    step_ns = max(1, 1000 // max(1, qd))

    rows = []
    t = 0
    seq_ptr = 0
    for i in range(count):
        if pattern == "seq_read":
            op, lba = "READ", seq_ptr
        elif pattern == "seq_write":
            op, lba = "WRITE", seq_ptr
        elif pattern == "rand_read":
            op, lba = "READ", rng.randrange(0, working_set)
        elif pattern == "rand_write":
            op, lba = "WRITE", rng.randrange(0, working_set)
        elif pattern == "mix_70r30w":
            op = "READ" if rng.random() < 0.7 else "WRITE"
            lba = rng.randrange(0, working_set)
        elif pattern == "mix_90r10w":
            op = "READ" if rng.random() < 0.9 else "WRITE"
            lba = rng.randrange(0, working_set)
        elif pattern == "hot_cold":
            # 90% of accesses hit the hottest 10% of the working set.
            hot_size = max(pages_per_req, working_set // 10)
            if rng.random() < 0.9:
                lba = rng.randrange(0, hot_size)
            else:
                lba = rng.randrange(hot_size, working_set)
            op = "READ" if rng.random() < 0.5 else "WRITE"
        elif pattern == "bursty_write":
            # Dense burst of 50 writes, then a gap long enough for the
            # device to actually drain its queue (device write latency is
            # on the order of ~600us; a sub-us gap wouldn't create real
            # idle time, so use a multi-ms quiet stretch every 50 writes).
            op = "WRITE"
            lba = rng.randrange(0, working_set)
            if i % 50 == 0 and i > 0:
                t += 20_000_000
        else:
            raise ValueError(f"unknown pattern: {pattern}")

        lba = min(lba, capacity_pages - pages_per_req)
        rows.append((t, op, lba, block_size_bytes, 0))
        t += step_ns
        seq_ptr = (seq_ptr + pages_per_req) % max(1, capacity_pages - pages_per_req)

    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pattern", choices=PATTERNS, required=True)
    ap.add_argument("--capacity-pages", type=int, required=True)
    ap.add_argument("--count", type=int, required=True)
    ap.add_argument("--block-size", type=int, default=4096)
    ap.add_argument("--page-size", type=int, default=4096)
    ap.add_argument("--working-set-frac", type=float, default=0.8)
    ap.add_argument("--qd", type=int, default=16)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    rows = gen(args.pattern, args.capacity_pages, args.count, args.block_size,
               args.page_size, args.working_set_frac, args.qd, args.seed)

    with open(args.out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["arrival_ns", "op", "lba", "size_bytes", "stream_id"])
        w.writerows(rows)

    print(f"wrote {len(rows)} requests to {args.out} (pattern={args.pattern}, seed={args.seed})",
          file=sys.stderr)


if __name__ == "__main__":
    main()
