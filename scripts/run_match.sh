#!/usr/bin/env bash
# Runs Match Director on a section of a match recording.
# Usage: bash scripts/run_match.sh <input.mp4> <start_s> <duration_s> <out_dir> [extra flags...]
# Example (WSL): bash scripts/run_match.sh assets/117092_panorama_1st_half.mp4 600 60 ~/md_out/demo
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
input="$(realpath "$1")"
start="$2"
duration="$3"
out="$4"
shift 4

mkdir -p "$out"
cd "$out"
"$HOME/build/match_director/release/src/match_director" \
  --input "$input" \
  --config "$repo/config/panorama_117092.json" \
  --events "$repo/config/demo_events.json" \
  --start "$start" --duration "$duration" \
  --output program.mp4 --clip-dir replays \
  --stats stats.json --camera-log camera.csv \
  "$@"
