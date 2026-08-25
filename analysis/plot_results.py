#!/usr/bin/env python3
"""Plot figures from a processed results CSV (one row per policy/config)."""
import argparse
import csv

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

POLICY_LABEL = {
    "foreground": "Foreground\nGreedy",
    "fixed_background": "Fixed\nBackground",
    "queue_aware": "Queue-Aware",
}
POLICY_ORDER = ["foreground", "fixed_background", "queue_aware"]


def load(csv_path):
    with open(csv_path) as f:
        rows = {r["label"]: r for r in csv.DictReader(f)}
    return rows


def bar_by_policy(rows, field, ylabel, title, out_path, log=False):
    labels = [p for p in POLICY_ORDER if p in rows]
    values = [float(rows[p][field]) for p in labels]
    fig, ax = plt.subplots(figsize=(5, 4))
    ax.bar([POLICY_LABEL[p] for p in labels], values, color=["#888", "#c44", "#3a7"])
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    if log:
        ax.set_yscale("log")
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    print(f"wrote {out_path}")


def line_by_key(csv_path, key, field, xlabel, ylabel, title, out_path, log_x=False, log_y=False):
    with open(csv_path) as f:
        rows = list(csv.DictReader(f))
    xs = [float(r[key]) for r in rows]
    ys = [float(r[field]) for r in rows]
    fig, ax = plt.subplots(figsize=(5, 4))
    ax.plot(xs, ys, marker="o", color="#3a7")
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    if log_x:
        ax.set_xscale("log")
    if log_y:
        ax.set_yscale("log")
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    print(f"wrote {out_path}")


def waf_p99_tradeoff(op_csv_path, out_path):
    with open(op_csv_path) as f:
        rows = list(csv.DictReader(f))
    waf = [float(r["waf"]) for r in rows]
    p99 = [float(r["p99_latency_ns"]) for r in rows]
    labels = [r["op_pct"] + "%" for r in rows]
    fig, ax = plt.subplots(figsize=(5, 4))
    ax.plot(waf, p99, marker="o", color="#c44")
    for w, p, lab in zip(waf, p99, labels):
        ax.annotate(f"OP={lab}", (w, p), textcoords="offset points", xytext=(6, 4))
    ax.set_xlabel("WAF")
    ax.set_ylabel("p99 latency (ns)")
    ax.set_title("WAF vs. p99 latency trade-off across over-provisioning")
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    print(f"wrote {out_path}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--policy-csv", default="results/processed/exp1_bursty_policy_comparison.csv")
    ap.add_argument("--qd-csv", default="results/processed/exp2_queue_depth_sweep.csv")
    ap.add_argument("--op-csv", default="results/processed/exp3_op_sweep.csv")
    ap.add_argument("--out-dir", default="figures")
    args = ap.parse_args()

    rows = load(args.policy_csv)
    bar_by_policy(rows, "p99_latency_ns", "p99 latency (ns)",
                   "Policy comparison — p99 latency (random write, GC pressure)",
                   f"{args.out_dir}/fig1_policy_p99_latency.png", log=True)
    bar_by_policy(rows, "waf", "Write amplification factor",
                   "Policy comparison — WAF",
                   f"{args.out_dir}/fig2_policy_waf.png", log=True)
    bar_by_policy(rows, "gc_induced_stall_ns", "GC-induced stall (ns, foreground GC only)",
                   "Policy comparison — foreground GC stall",
                   f"{args.out_dir}/fig3_policy_gc_stall.png", log=False)

    line_by_key(args.qd_csv, "qd", "throughput_reqs_per_sec", "Queue depth",
                "Throughput (req/s)", "Throughput vs. queue depth",
                f"{args.out_dir}/fig4_qd_throughput.png", log_x=True)
    line_by_key(args.qd_csv, "qd", "p99_latency_ns", "Queue depth",
                "p99 latency (ns)", "p99 latency vs. queue depth",
                f"{args.out_dir}/fig5_qd_p99_latency.png", log_x=True, log_y=True)
    line_by_key(args.op_csv, "op_pct", "waf", "Over-provisioning (%)",
                "Write amplification factor", "WAF vs. over-provisioning",
                f"{args.out_dir}/fig6_op_waf.png")
    waf_p99_tradeoff(args.op_csv, f"{args.out_dir}/fig7_waf_p99_tradeoff.png")


if __name__ == "__main__":
    main()
