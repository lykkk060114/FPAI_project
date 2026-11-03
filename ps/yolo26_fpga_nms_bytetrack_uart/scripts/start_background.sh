#!/usr/bin/env bash
set -euo pipefail

package_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
config_path="${1:-$package_dir/configs/sdicamera+yolo26+bytetrack+classify_zg330+range+hdmi_60.yaml}"
cd "$package_dir"
mkdir -p results
export LD_LIBRARY_PATH="$package_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

binary="$package_dir/bin/sdicamera+yolo26+bytetrack+hdmi"
process_pattern='^([^[:space:]]*/)?sdicamera[+]'
if ! flock -n results/observation.lock true; then
  echo "Observation or manual Qwen generation is still running" >&2
  exit 1
fi
if pgrep -f "$process_pattern" >/dev/null; then
  echo "An SDI inference process is already running; stop it first" >&2
  pgrep -af "$process_pattern"
  exit 1
fi
if [[ -f results/bytetrack.log ]]; then
  mv results/bytetrack.log "results/bytetrack_$(date -u +%Y%m%dT%H%M%SZ)_$$.log"
fi

nohup python3 tools/run_observation.py "$config_path" </dev/null \
  >results/bytetrack.log 2>&1 &
echo $! >results/bytetrack.pid
echo "started launcher pid=$(cat results/bytetrack.pid)"
echo "log: $package_dir/results/bytetrack.log"
