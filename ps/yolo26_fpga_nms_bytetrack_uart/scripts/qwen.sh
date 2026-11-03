#!/usr/bin/env bash
set -euo pipefail
package_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# Preserve the configured SSH return destination for manually generated results.
if [[ -r /etc/default/xunjing ]]; then
  set -a
  source /etc/default/xunjing
  set +a
fi
exec python3 "$package_dir/tools/qwen.py" "$@"
