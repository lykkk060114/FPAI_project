#!/usr/bin/env bash
set -euo pipefail

package_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
: "${FPAI_ROOT:?set FPAI_ROOT to fpai_demo_package_26040701}"
mkdir -p "$package_dir/bin"

docker run --rm \
  -v "$package_dir/src:/work/src:ro" \
  -v "$FPAI_ROOT:/work/fpai_demo_package_26040701:ro" \
  -v "$package_dir/deps:/packages:ro" \
  -v "$package_dir/bin:/out" \
  fpai-cross:20.04 bash -lc '
    set -e
    dpkg-deb -x /packages/Icraft_3.36.1_onchip.deb /tmp/sdk
    dpkg-deb -x /packages/CustomOp_3.36.1_onchip.deb /tmp/sdk
    cmake -S /work/src -B /tmp/xunjing-build \
      -DXUNJING_BUILD_FPAI_APP=ON -DXUNJING_PLIN_FP32_BRIDGE=ON \
      -DFPAI_ROOT=/work/fpai_demo_package_26040701 \
      -DCMAKE_SYSTEM_NAME=Linux \
      -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
      -DCMAKE_C_COMPILER=/usr/bin/aarch64-linux-gnu-gcc \
      -DCMAKE_CXX_COMPILER=/usr/bin/aarch64-linux-gnu-g++ \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=/tmp/sdk/usr \
      -DIcraft-BuyiBackend_DIR=/tmp/sdk/usr/cmake \
      -DIcraft-HostBackend_DIR=/tmp/sdk/usr/cmake \
      -Dicraft-zg330backend_DIR=/tmp/sdk/usr/cmake
    cmake --build /tmp/xunjing-build -j28
    cp /tmp/xunjing-build/sdicamera+yolo26+bytetrack+hdmi /out/
    cp /tmp/xunjing-build/classifier_zg330 /out/
    cp /tmp/xunjing-build/classifier_benchmark /out/
    cp /tmp/xunjing-build/detector_replay /out/
    cp /tmp/xunjing-build/sdi_capture /out/
    cp /tmp/xunjing-build/pl_tasks_check /out/
    cp /tmp/xunjing-build/pl_acceptance /out/
    cp /tmp/xunjing-build/pl_post_v2_acceptance /out/
    file /out/classifier_zg330
    file /out/sdicamera+yolo26+bytetrack+hdmi
  '

mkdir -p "$package_dir/lib"
cp "$FPAI_ROOT/deps/thirdparty/so/onnxruntime/libonnxruntime.so.1.17.1" \
   "$package_dir/lib/"
cp "$FPAI_ROOT/deps/thirdparty/so/onnxruntime/libonnxruntime.so" \
   "$package_dir/lib/"

python3 "$package_dir/tools/package_runtime.py"
python3 "$package_dir/tools/package_runtime.py" --check
