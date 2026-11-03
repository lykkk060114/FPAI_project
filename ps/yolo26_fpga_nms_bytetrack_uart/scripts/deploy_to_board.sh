#!/usr/bin/env bash
set -euo pipefail
package_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
board="${1:-root@192.168.126.49}"
# Fixed destination keeps YOLO26 separate from YOLOv5.
destination=/root/workspace/yolo26_fpga_nms_bytetrack_uart
upload_dir="$(mktemp -d -t fmql-deploy.XXXXXXXX)"
remote_stage=""
cleanup() {
  rm -rf -- "$upload_dir"
  if [[ "$remote_stage" == /tmp/fmql-deploy.* ]]; then
    ssh -o BatchMode=yes -o ConnectTimeout=8 "$board" "rm -rf -- '$remote_stage'" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT
python3 "$package_dir/tools/package_runtime.py" --check --archive "$upload_dir/runtime.tar.gz"
ssh_options=(-o BatchMode=yes -o ConnectTimeout=8)
remote_stage="$(ssh "${ssh_options[@]}" "$board" 'mktemp -d /tmp/fmql-deploy.XXXXXXXX')"
[[ "$remote_stage" =~ ^/tmp/fmql-deploy\.[a-zA-Z0-9]+$ ]] || { echo 'Unexpected staging path' >&2; exit 1; }
scp -q "${ssh_options[@]}" "$upload_dir/runtime.tar.gz" "$board:$remote_stage/runtime.tar.gz"
ssh "${ssh_options[@]}" "$board" "bash -s -- '$remote_stage' '$destination'" <<'REMOTE'
set -euo pipefail
stage="$1"
destination="$2"
# Refuse replacing a running binary, including foreground launches using ./bin/...
python3 - <<'PYREMOTE'
from pathlib import Path
for proc in Path('/proc').glob('[0-9]*'):
    try:
        command = (proc / 'cmdline').read_bytes().split(b'\0')
        if command and Path(command[0].decode()).name.startswith('sdicamera+'):
            raise SystemExit('Inference is running; stop it before deployment (PID ' + proc.name + ')')
    except (OSError, UnicodeError):
        pass
PYREMOTE
cd "$stage"
tar -xzf runtime.tar.gz
sha256sum -c SHA256SUMS > verify.log
rm runtime.tar.gz verify.log
mkdir -p "$destination"
# Existing results, calibration files outside configs, and YOLOv5 stay in place.
cp -a . "$destination/"
cd "$destination"
chmod +x bin/* scripts/*.sh
sha256sum -c SHA256SUMS > /tmp/fmql-deployed-checksums.log
export LD_LIBRARY_PATH="$destination/lib:${LD_LIBRARY_PATH:-}"
ldd bin/sdicamera+yolo26+bytetrack+hdmi > /tmp/fmql-deployed-libraries.log
if grep -q 'not found' /tmp/fmql-deployed-libraries.log; then
    cat /tmp/fmql-deployed-libraries.log
    exit 1
fi
printf 'Deployed and verified: %s\n' "$destination"
REMOTE
