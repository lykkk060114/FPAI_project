#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
board="${1:-root@192.168.126.49}"
base=/root/workspace/qwen_behavior_summary
# Generate checksums from the prepared runtime and the current source before SSH.
python3 - "$root" <<'PY'
import hashlib
import sys
from pathlib import Path
root = Path(sys.argv[1])
for name in ('runtime/config.json', 'bin/qwen_icraft'):
    if not (root / name).is_file():
        raise SystemExit('Missing Qwen deployment asset: ' + str(root / name))
if not list((root / 'deps').glob('tokenizers-*.whl')):
    raise SystemExit('Missing ARM64 tokenizers wheel in ' + str(root / 'deps'))
paths = []
for directory in ('runtime', 'bin', 'deps', 'src'):
    paths.extend(p for p in (root / directory).rglob('*') if p.is_file())
paths.extend(root / name for name in ('README.md', 'tools/generate_summary_zg330.py',
    'tools/snapshot_cache.py', 'tools/build_runtime.sh', 'tools/deploy.sh',
    'tools/test_runner.py', 'tools/test_snapshot_cache.py'))
def checksum(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()
(root / 'SHA256SUMS').write_text(''.join(
    f'{checksum(p)}  {p.relative_to(root)}\n' for p in sorted(paths)))
parent = root.parent
(parent / 'SHA256SUMS').write_text(''.join(
    f'{checksum(parent / name)}  {name}\n' for name in ('scripts/generate_summary.py', 'settings.json')))
PY
ssh "$board" "mkdir -p '$base/zg330' '$base/scripts'; if test -f '$base/scripts/generate_summary.py'; then cp -p '$base/scripts/generate_summary.py' '$base/scripts/generate_summary_before_zg330.py'; fi"
tar -C "$root" -cf - runtime bin deps src README.md tools/generate_summary_zg330.py tools/snapshot_cache.py tools/build_runtime.sh tools/deploy.sh tools/test_runner.py tools/test_snapshot_cache.py SHA256SUMS | ssh "$board" "tar -C '$base/zg330' -xf -"
scp "$root/../scripts/generate_summary.py" "$board:$base/scripts/"
scp "$root/../SHA256SUMS" "$board:$base/"
scp "$root/../settings.json" "$board:$base/"
ssh "$board" "cd '$base/zg330' && sha256sum -c SHA256SUMS && python3 -m pip install --no-deps --no-index deps/tokenizers-*.whl && ldd bin/qwen_icraft"
