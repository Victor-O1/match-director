#!/usr/bin/env bash
# Builds the release preset, runs all benchmarks, and saves the results with
# the hardware they ran on to bench/results/<date>_<host>.json + .txt.
# Usage (from repo root, inside WSL/Linux):  bash bench/run_bench.sh
set -euo pipefail

cd "$(dirname "$0")/.."
cmake --preset release >/dev/null
cmake --build --preset release --target md_bench

stamp="$(date +%Y-%m-%d_%H%M)_$(hostname)"
mkdir -p bench/results
txt="bench/results/${stamp}.txt"

{
  echo "date: $(date -Iseconds)"
  echo "git: $(git rev-parse --short HEAD 2>/dev/null || echo 'no-commit')"
  echo "cpu: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | xargs)"
  echo "logical cpus: $(nproc)"
  echo "kernel: $(uname -r)"
  echo "compiler: $(c++ --version | head -1)"
  grep -qi microsoft /proc/version && echo "note: running under WSL2"
  echo
} > "$txt"

"$HOME/build/match_director/release/bench/md_bench" \
  --benchmark_repetitions=3 --benchmark_report_aggregates_only=true \
  --benchmark_out="bench/results/${stamp}.json" --benchmark_out_format=json \
  | tee -a "$txt"

echo "saved: $txt"
