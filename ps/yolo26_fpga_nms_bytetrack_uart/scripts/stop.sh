#!/usr/bin/env bash
set -euo pipefail

package_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if command -v systemctl >/dev/null &&
   [[ "$(systemctl show xunjing.service -p WorkingDirectory --value 2>/dev/null || true)" == "$package_dir" ]] &&
   [[ "$(systemctl show xunjing.service -p ActiveState --value 2>/dev/null || true)" =~ ^(active|activating|deactivating)$ ]]; then
  echo 'Stopping xunjing.service; saving statistics to /root/result_json'
  exec systemctl stop xunjing.service
fi
binary="$package_dir/bin/sdicamera+yolo26+bytetrack+hdmi"
process_pattern='^([^[:space:]]*/)?sdicamera[+]yolo26[+]bytetrack[+]hdmi([+]capture)?([[:space:]]|$)'
mapfile -t inference_pids < <(pgrep -f "$process_pattern" || true)
for inference_pid in "${inference_pids[@]}"; do
  kill -TERM "$inference_pid" 2>/dev/null || true
done
if [[ -f "$package_dir/results/bytetrack.pid" ]]; then
  launcher_pid="$(cat "$package_dir/results/bytetrack.pid")"
  # An old PID file can survive reboot: never signal a reused unrelated PID.
  if [[ "$launcher_pid" =~ ^[0-9]+$ ]] && (( launcher_pid > 1 )) &&
      [[ -r "/proc/$launcher_pid/cmdline" ]] &&
      tr '\0' ' ' <"/proc/$launcher_pid/cmdline" | grep -Fq -- "$binary"; then
    pkill -TERM -P "$launcher_pid" 2>/dev/null || true
    kill -TERM "$launcher_pid" 2>/dev/null || true
  fi
  rm -f "$package_dir/results/bytetrack.pid"
fi
for attempt in {1..5}; do
  if ! pgrep -f "$process_pattern" >/dev/null; then
    echo "stopped ByteTrack inference; observation launcher will save statistics only"
    echo "view report: python3 tools/show_behavior_report.py"
    exit 0
  fi
  sleep 1
done
pkill -KILL -f "$process_pattern" 2>/dev/null || true
echo "stopped ByteTrack inference"
