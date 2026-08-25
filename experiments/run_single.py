#!/usr/bin/env python3
"""Run one ftlsim_cli invocation and return its single CSV result row as a dict."""
import csv
import io
import subprocess
import sys


def run(binary, nand_conf, ftl_conf, trace, policy=None, label="run"):
    cmd = [binary, "--nand", nand_conf, "--ftl", ftl_conf, "--trace", trace, "--label", label]
    if policy:
        cmd += ["--policy", policy]
    out = subprocess.run(cmd, capture_output=True, text=True, check=True)
    reader = csv.DictReader(io.StringIO(out.stdout))
    return next(reader)


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default="build/ftlsim_cli")
    ap.add_argument("--nand", default="configs/nand.conf")
    ap.add_argument("--ftl", default="configs/ftl_queue_aware.conf")
    ap.add_argument("--trace", required=True)
    ap.add_argument("--policy")
    ap.add_argument("--label", default="run")
    args = ap.parse_args()
    row = run(args.bin, args.nand, args.ftl, args.trace, args.policy, args.label)
    for k, v in row.items():
        print(f"{k}: {v}")
