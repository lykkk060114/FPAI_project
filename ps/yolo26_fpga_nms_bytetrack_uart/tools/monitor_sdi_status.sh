#!/usr/bin/env bash
# Read-only monitor for the PLIN-to-PS SDI capture status block.
set -euo pipefail

samples=${1:-10}
interval_seconds=${2:-0.1}

if ! [[ "$samples" =~ ^[1-9][0-9]*$ ]]; then
  echo "usage: $0 [positive sample count] [interval seconds]" >&2
  exit 2
fi

if command -v devmem >/dev/null 2>&1; then
  read_reg() { devmem "$1" 32; }
elif command -v busybox >/dev/null 2>&1 && busybox devmem 0x40080058 32 >/dev/null 2>&1; then
  read_reg() { busybox devmem "$1" 32; }
else
  # Some official images do not ship devmem. /dev/mem is still read-only here.
  read_reg() {
    python3 - "$1" <<'PY'
import mmap
import os
import struct
import sys

addr = int(sys.argv[1], 0)
page = mmap.PAGESIZE
base = addr & ~(page - 1)
offset = addr - base
fd = os.open('/dev/mem', os.O_RDONLY | os.O_SYNC)
mem = mmap.mmap(fd, page, mmap.MAP_SHARED, mmap.PROT_READ, offset=base)
try:
    print(f'0x{struct.unpack_from("<I", mem, offset)[0]:08x}')
finally:
    mem.close()
    os.close(fd)
PY
  }
fi

printf 'time,take,write_dma,done,cycle_counter,timeout_counter\n'
for _ in $(seq 1 "$samples"); do
  printf '%s,%s,%s,%s,%s,%s\n' \
    "$(date -Is)" \
    "$(read_reg 0x40080004)" \
    "$(read_reg 0x40080050)" \
    "$(read_reg 0x40080058)" \
    "$(read_reg 0x40080084)" \
    "$(read_reg 0x40080090)"
  sleep "$interval_seconds"
done
