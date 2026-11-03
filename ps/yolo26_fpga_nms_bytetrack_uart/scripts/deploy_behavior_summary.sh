#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
board="${1:-root@192.168.126.49}"
bash "$root/scripts/deploy_to_board.sh" "$board"
bash "$root/../qwen_behavior_summary/scripts/deploy_to_board.sh" "$board"
python3 "$root/tools/sync_observation_clock.py" "$board"
