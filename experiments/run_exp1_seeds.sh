#!/usr/bin/env bash
# Exp1 multi-seed: policy comparison under bursty write. Traces are regenerable
# from (pattern, capacity-pages, count, working-set-frac, qd, seed) so they are
# written to a scratch dir, not the repo. Only the per-run metric rows land in results/.
set -euo pipefail
cd "$(dirname "$0")/.."
SCRATCH="${FTLSIM_SCRATCH:-/tmp/ftlsim-traces}"
mkdir -p "$SCRATCH" results/raw results/manifests
OUT=results/raw/exp1_bursty_multiseed.csv
: > "$OUT"
first=1
for seed in 0 1 2 3 4; do
  trace="$SCRATCH/exp1_bursty_seed${seed}.csv"
  [ -f "$trace" ] || python3 workloads/generator.py --pattern bursty_write \
      --capacity-pages 900000 --count 2700000 --working-set-frac 1.0 --qd 16 \
      --seed "$seed" --out "$trace"
  for policy in foreground fixed_background queue_aware; do
    row=$(./build/ftlsim_cli --nand configs/nand.conf --ftl "configs/ftl_${policy}.conf" \
          --trace "$trace" --label "${policy}_seed${seed}")
    if [ $first -eq 1 ]; then echo "$row" | head -1 >> "$OUT"; first=0; fi
    echo "$row" | tail -1 >> "$OUT"
    echo "done ${policy} seed${seed}" >&2
  done
done
echo "wrote $OUT" >&2
