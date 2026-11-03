#!/usr/bin/env bash
set -euo pipefail

package_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
config_path="${1:-$package_dir/configs/sdicamera+yolo26+bytetrack+classify_zg330+range+hdmi_60.yaml}"
cd "$package_dir"
mkdir -p results
if ! flock -n results/observation.lock true; then
  echo "Observation or manual Qwen generation is still running" >&2
  exit 1
fi
export LD_LIBRARY_PATH="$package_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

if pgrep -f '^([^[:space:]]*/)?sdicamera[+]' >/dev/null; then
  echo "An SDI inference process is already running; stop it first" >&2
  exit 1
fi
exec python3 tools/run_observation.py "$config_path"
