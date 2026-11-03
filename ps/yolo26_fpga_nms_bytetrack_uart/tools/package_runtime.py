#!/usr/bin/env python3
"""Produce one deployable file set and checksum manifest; exclude training/results."""
import argparse
import hashlib
from pathlib import Path
import tarfile

ROOT = Path(__file__).resolve().parents[1]
DIRECTORIES = ('bin', 'configs', 'deps', 'imodel', 'names', 'lib', 'src', 'scripts', 'tools')
DOCS = ('README.md', 'FPGA_DATA_FLOW.md', 'STAGE2_INTERFACE_CONFIRMATION.md')

def files():
    selected = set()
    for directory in DIRECTORIES:
        selected.update(p for p in (ROOT / directory).rglob('*')
                        if p.is_file() and '__pycache__' not in p.parts and p.suffix != '.pyc')
    selected.update(ROOT / name for name in DOCS if (ROOT / name).is_file())
    return sorted(selected)

def checksum(path):
    digest = hashlib.sha256()
    with path.open('rb') as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()

def manifest():
    return ''.join(f'{checksum(p)}  {p.relative_to(ROOT)}\n' for p in files())

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    data = manifest()
    destination = ROOT / 'SHA256SUMS'
    if args.check:
        if not destination.exists() or destination.read_text() != data:
            raise SystemExit('Runtime files differ from SHA256SUMS; regenerate after reviewing changes')
    else:
        temporary = ROOT / 'SHA256SUMS.pending'
        temporary.write_text(data)
        temporary.replace(destination)
    if args.archive:
        args.archive.parent.mkdir(parents=True, exist_ok=True)
        with tarfile.open(args.archive, 'w:gz', dereference=True) as archive:
            for path in files() + [destination]:
                info = archive.gettarinfo(str(path), str(path.relative_to(ROOT)))
                info.mtime = 0  # Board RTC may be unset; avoid misleading future timestamps.
                with path.open('rb') as handle:
                    archive.addfile(info, handle)
    print(f'{len(data.splitlines())} runtime files verified' if args.check else
          f'{len(data.splitlines())} runtime files checksummed')

if __name__ == '__main__':
    main()
