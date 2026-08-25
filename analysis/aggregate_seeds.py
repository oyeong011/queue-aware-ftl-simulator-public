#!/usr/bin/env python3
"""Aggregate a multi-seed run (label = <policy>_seed<N>) into mean/stdev per policy,
write a processed CSV, and redraw the policy figures with error bars.

Single-seed bars hide whether a 5% difference is signal or noise, so the policy
comparison figures are generated from this, not from a one-seed CSV.
"""
import argparse
import csv
import statistics as st

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

ORDER = ["foreground", "fixed_background", "queue_aware"]
LABEL = {
    "foreground": "Foreground\nGreedy",
    "fixed_background": "Fixed\nBackground",
    "queue_aware": "Queue-Aware",
}
FIELDS = ["p99_latency_ns", "waf", "gc_induced_stall_ns", "p95_latency_ns",
          "avg_latency_ns", "throughput_reqs_per_sec", "gc_count", "pages_moved_by_gc"]


def group(path):
    by = {}
    for r in csv.DictReader(open(path)):
        by.setdefault(r["label"].rsplit("_seed", 1)[0], []).append(r)
    return by


def stats(rows, field):
    v = [float(r[field]) for r in rows]
    return st.mean(v), (st.stdev(v) if len(v) > 1 else 0.0), len(v)


def bar(by, field, ylabel, title, out, scale=1.0, log=False):
    pols = [p for p in ORDER if p in by]
    mus, sds = [], []
    for p in pols:
        m, s, _ = stats(by[p], field)
        mus.append(m / scale)
        sds.append(s / scale)
    fig, ax = plt.subplots(figsize=(5, 4))
    ax.bar([LABEL[p] for p in pols], mus, yerr=sds, capsize=5,
           color=["#888", "#c44", "#3a7"])
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    if log:
        ax.set_yscale("log")
    fig.tight_layout()
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"wrote {out}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--raw", default="results/raw/exp1_bursty_multiseed.csv")
    ap.add_argument("--out-csv", default="results/processed/exp1_multiseed_summary.csv")
    ap.add_argument("--fig-dir", default="figures")
    a = ap.parse_args()

    by = group(a.raw)
    with open(a.out_csv, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["policy", "metric", "mean", "stdev", "n"])
        for p in ORDER:
            if p not in by:
                continue
            for fld in FIELDS:
                m, s, n = stats(by[p], fld)
                w.writerow([p, fld, f"{m:.6g}", f"{s:.6g}", n])
    print(f"wrote {a.out_csv}")

    bar(by, "p99_latency_ns", "p99 latency (s)",
        "p99 latency by GC policy (5 seeds, ±1σ)",
        f"{a.fig_dir}/fig1_policy_p99_latency.png", scale=1e9, log=True)
    bar(by, "waf", "Write amplification factor",
        "WAF by GC policy (5 seeds, ±1σ)",
        f"{a.fig_dir}/fig2_policy_waf.png", log=True)
    bar(by, "gc_induced_stall_ns", "GC-induced stall (s)",
        "GC-induced host stall by policy (5 seeds, ±1σ)",
        f"{a.fig_dir}/fig3_policy_gc_stall.png", scale=1e9)

    # Paired per-seed deltas: more informative than comparing two noisy means.
    print("\nper-seed delta, queue_aware vs foreground (negative = queue_aware better)")
    for fld in ["p99_latency_ns", "gc_induced_stall_ns", "waf"]:
        f0 = [float(r[fld]) for r in by["foreground"]]
        q0 = [float(r[fld]) for r in by["queue_aware"]]
        d = [(q - f) / f * 100 for f, q in zip(f0, q0)]
        print(f"  {fld:22} mean {st.mean(d):+6.2f}%  sd {st.stdev(d):5.2f}%  "
              f"per-seed {[round(x, 2) for x in d]}")


if __name__ == "__main__":
    main()
