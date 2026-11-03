#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
: "${ICRAFT_PACKAGES:?set ICRAFT_PACKAGES to the directory containing Icraft_3.36.1_onchip.deb}"
mkdir -p "$root/bin"
docker run --rm -v "$root:/work" -v "$ICRAFT_PACKAGES:/packages:ro" fpai-cross:20.04 bash -lc '
set -e
dpkg-deb -x /packages/Icraft_3.36.1_onchip.deb /tmp/sdk
cmake -S /work/src -B /tmp/qwen-build -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ -DCMAKE_BUILD_TYPE=Release -DICRAFT_SDK=/tmp/sdk/usr
cmake --build /tmp/qwen-build -j28
cp /tmp/qwen-build/qwen_icraft /work/bin/
file /work/bin/qwen_icraft
'
