#!/usr/bin/env bash
set -euo pipefail

package_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
binary="$package_dir/bin/sdicamera+yolo26+bytetrack+hdmi"
cd "$package_dir"
export LD_LIBRARY_PATH="$package_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

echo "== architecture =="
uname -m
file "$binary"

echo "== runtime libraries =="
library_report="$(ldd "$binary")"
echo "$library_report"
if [[ "$library_report" == *"not found"* ]]; then
  echo "Missing runtime libraries; resolve these before starting inference" >&2
  exit 1
fi

echo "== model files =="
sha256sum -c "$package_dir/SHA256SUMS"

echo "== FPGA and DMA devices =="
ls -l /dev/uio* /dev/udmabuf0 2>/dev/null || true

echo "== UART devices =="
ls -l /dev/ttyUSB* /dev/ttyACM* /dev/ttyS* /dev/ttyPS* 2>/dev/null || true

echo "== existing inference processes =="
pgrep -af 'sdicamera\+yolo26|yolo26|bytetrack' || true
