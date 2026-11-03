#!/usr/bin/env bash
set -euo pipefail
package_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$package_dir"
export PYTHONUNBUFFERED=1
config="${XUNJING_CONFIG:-$package_dir/configs/sdicamera+yolo26+bytetrack+classify_zg330+range+hdmi_60.yaml}"
[[ -r "$config" && -x "$package_dir/bin/sdicamera+yolo26+bytetrack+hdmi" ]]
exec bash "$package_dir/scripts/run_foreground.sh" "$config"
