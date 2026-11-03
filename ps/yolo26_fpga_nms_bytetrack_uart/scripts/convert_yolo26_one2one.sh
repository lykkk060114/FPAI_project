#!/usr/bin/env bash
# Uses the prepared Linux SDK and Windows codegen Docker containers.
# Six one-to-one Conv outputs, BF16 NPU, FP32 outputs for CPU reference or PL POST.
set -euo pipefail
package_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
project_dir="$(cd "$package_dir/../.." && pwd)"
python_bin="${FMQL_TRAIN_PYTHON:-/home/lykkk/work_space/ai_train/yolov5/.venv/bin/python}"
linux_container="${FMQL_LINUX_CONTAINER:-fmql-icraft-convert-20261012}"
windows_container="${FMQL_WINDOWS_CONTAINER:-fmql-windows-codegen-20261013}"
weights="${1:-$package_dir/training/detection/output/panda_yolo26s-2/best.pt}"
output_dir="$(realpath -m "${2:-$package_dir/training/detection/converted_20261013}")"
calibration_dir="$(realpath "${3:-$package_dir/training/detection/converted_20261012/calibration}")"
[[ "$output_dir" == "$project_dir/"* && "$calibration_dir" == "$project_dir/"* ]]
mkdir -p "$output_dir"
OMP_NUM_THREADS=8 "$python_bin" "$package_dir/training/detection/export_yolo26.py" \
  "$weights" --output "$output_dir/e2e" --opset 17 > "$output_dir/export.log" 2>&1
"$python_bin" "$package_dir/training/detection/extract_yolo26_conv.py" \
  "$output_dir/e2e/yolo26_panda_352x640.onnx" "$output_dir/yolo26_panda_conv.onnx" \
  > "$output_dir/extract.log"
container_output="/project/${output_dir#"$project_dir/"}"
container_calibration="/project/${calibration_dir#"$project_dir/"}"
docker exec "$linux_container" bash -c '
  set -euo pipefail
  b="$1"; calibration="$2"; name=yolo26_panda_conv
  icraft-parse --net_name "$name" --network "$b/$name.onnx" \
    --framework ONNX --target Zhuge --inputs 1,352,640,3 --inputs_layout NHWC \
    --inputs_dtype UInt8 --pre_method resize --pre_scale 255,255,255 \
    --pre_mean 0,0,0 --channel_swap 2,1,0 --jr_path "$b/parsed" > "$b/parse.log" 2>&1
  icraft-optimize --json "$b/parsed/${name}_parsed.json" \
    --raw "$b/parsed/${name}_parsed.raw" --target ZHUGE \
    --jr_path "$b/optimized" > "$b/optimize.log" 2>&1
  icraft-quantize --json "$b/optimized/${name}_optimized.json" \
    --raw "$b/optimized/${name}_optimized.raw" --target ZHUGE --qdtype bf16 \
    --forward_dir "$calibration" --jr_path "$b/quantized" > "$b/quantize.log" 2>&1
  icraft-adapt --json "$b/quantized/${name}_quantized.json" \
    --raw "$b/quantized/${name}_quantized.raw" --target ZHUGE \
    --jr_path "$b/adapted" > "$b/adapt.log" 2>&1
' _ "$container_output" "$container_calibration"
docker exec "$windows_container" bash -c '
  set -euo pipefail
  b="$1"; win="Z:${b//\//\\}"
  export WINEPREFIX=/tmp/fmql-wine WINEDEBUG=-all
  export WINEDLLOVERRIDES="msvcp140,msvcp140_1,msvcp140_2,vcruntime140,vcruntime140_1,vcomp140,concrt140=n,b"
  # Codegen writes loop_params.json next to its executable: this directory must be writable.
  test -w /tmp/icraft-win-bin
  xvfb-run -a /usr/lib/wine/wine64 /tmp/icraft-win-bin/icraft-generate.exe \
    --json "$win\\adapted\\yolo26_panda_conv_adapted.json" \
    --raw "$win\\adapted\\yolo26_panda_conv_adapted.raw" \
    --jr_path "$win\\generated" > "$b/generate.log" 2>&1
' _ "$container_output"
docker exec "$linux_container" chown -R "$(id -u):$(id -g)" "$container_output"
printf '%s\n' "Generated: $output_dir/generated/yolo26_panda_conv_ZG.json and .raw"
