#!/usr/bin/env bash
# Reproduce this repository's results from a clean checkout.
#
#   ./experiments/reproduce_core.sh --mode smoke   # ~4 s     build + tests + tiny run
#   ./experiments/reproduce_core.sh --mode core    # ~1.5 min + Exp1 across 5 seeds, figures
#   ./experiments/reproduce_core.sh --mode full    # + Exp2/3/4 sweeps (not timed end-to-end)
#
# Traces are regenerated from (pattern, args, seed) into $FTLSIM_SCRATCH rather than
# read from the repo — they are ~90 MB each and fully determined by those values.
set -euo pipefail
cd "$(dirname "$0")/.."

MODE="smoke"
[ "${1:-}" = "--mode" ] && MODE="${2:-smoke}"
[ -n "${1:-}" ] && [ "${1:-}" != "--mode" ] && MODE="$1"

SCRATCH="${FTLSIM_SCRATCH:-/tmp/ftlsim-traces}"
CLI=./build/ftlsim_cli
mkdir -p "$SCRATCH" results/raw results/processed results/manifests figures

echo "=== [1/4] deps ==="
command -v cmake >/dev/null || { echo "cmake missing"; exit 1; }
command -v python3 >/dev/null || { echo "python3 missing"; exit 1; }
python3 -c "import matplotlib" 2>/dev/null || echo "WARN: matplotlib missing — figures will be skipped"
cmake --version | head -1
g++ --version | head -1

echo "=== [2/4] clean build ==="
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j"$(nproc)" >/dev/null
echo "binary: $(sha256sum $CLI | cut -c1-16)...  commit: $(git rev-parse --short HEAD 2>/dev/null || echo 'not-a-git-checkout')"

echo "=== [3/4] invariant tests ==="
ctest --test-dir build --output-on-failure

gen() {  # gen <pattern> <count> <ws_frac> <seed> <out>
  [ -f "$5" ] || python3 workloads/generator.py --pattern "$1" --capacity-pages 900000 \
      --count "$2" --working-set-frac "$3" --qd 16 --seed "$4" --out "$5"
}

echo "=== [4/4] experiments (mode=$MODE) ==="
case "$MODE" in
  smoke)
    gen bursty_write 100000 1.0 0 "$SCRATCH/smoke.csv"
    $CLI --nand configs/nand.conf --ftl configs/ftl_queue_aware.conf \
         --trace "$SCRATCH/smoke.csv" --label smoke
    echo "[smoke] build + 12 invariants + one real run OK. Reproduces no published number."
    ;;
  core|full)
    ./experiments/run_exp1_seeds.sh
    if [ "$MODE" = "full" ]; then
      # Exp2/3 processed CSVs carry a leading key column (qd / op_pct) that
      # analysis/plot_results.py plots against; the CLI does not emit it, so it is
      # prepended here.
      echo "--- Exp2: queue depth ---"
      gen mix_70r30w 200000 1.0 0 "$SCRATCH/exp2.csv"
      { first=1; for qd in 1 4 16 32; do
          row=$($CLI --nand configs/nand.conf --ftl configs/ftl_queue_aware.conf \
                --trace "$SCRATCH/exp2.csv" --qd "$qd" --label "qd${qd}")
          [ $first = 1 ] && { echo "qd,$(echo "$row" | head -1)"; first=0; }
          echo "${qd},$(echo "$row" | tail -1)"
        done; } > results/processed/exp2_queue_depth_sweep.csv
      echo "--- Exp3: over-provisioning ---"
      { first=1; for op in 7 14 28; do
          gen rand_write 2910000 1.0 0 "$SCRATCH/exp3_op${op}.csv"
          row=$($CLI --nand configs/nand.conf --ftl configs/ftl_queue_aware.conf \
                --trace "$SCRATCH/exp3_op${op}.csv" --op "$op" --label "op${op}")
          [ $first = 1 ] && { echo "op_pct,$(echo "$row" | head -1)"; first=0; }
          echo "${op},$(echo "$row" | tail -1)"
        done; } > results/processed/exp3_op_sweep.csv
      echo "--- Exp4: locality ---"
      gen hot_cold 2700000 1.0 0 "$SCRATCH/exp4_hotcold.csv"
      $CLI --nand configs/nand.conf --ftl configs/ftl_queue_aware.conf \
           --trace "$SCRATCH/exp4_hotcold.csv" --label hot_cold \
        > results/processed/exp4_locality.csv
      # fig4-7 from the sweeps. Runs BEFORE aggregate_seeds.py because it also
      # redraws fig1-3 from the old single-seed CSV; the multi-seed versions must win.
      python3 analysis/plot_results.py
    fi
    python3 analysis/aggregate_seeds.py
    echo
    echo "[$MODE] done. Headline numbers: results/processed/exp1_multiseed_summary.csv"
    echo "[$MODE] Manifest: results/manifests/exp1_multiseed.json"
    ;;
  *)
    echo "unknown mode: $MODE (smoke|core|full)"; exit 1;;
esac
