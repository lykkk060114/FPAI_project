#!/usr/bin/env bash
# Run in the Ubuntu 20.04 container with the official AMD64 iCraft SDK installed.
# Arguments: traditional-head opset-17 ONNX, calibration image directory, output directory.
set -euo pipefail
onnx_model="$(realpath "${1:?provide ONNX path}")"
calibration_dir="$(realpath "${2:?provide calibration directory}")"
output_dir="$(realpath -m "${3:?provide output directory}")"
name=yolo26_panda_352x640
mkdir -p "$output_dir"

icraft-parse --net_name "$name" --network "$onnx_model" \
  --framework ONNX --target Zhuge --inputs 1,352,640,3 \
  --inputs_layout NHWC --inputs_dtype UInt8 --pre_method resize \
  --pre_scale 255,255,255 --pre_mean 0,0,0 --channel_swap 2,1,0 \
  --jr_path "$output_dir/parsed" > "$output_dir/parse.log" 2>&1
icraft-optimize --json "$output_dir/parsed/${name}_parsed.json" \
  --raw "$output_dir/parsed/${name}_parsed.raw" --target ZHUGE \
  --jr_path "$output_dir/optimized_correct" > "$output_dir/optimize.log" 2>&1
icraft-quantize --json "$output_dir/optimized_correct/${name}_optimized.json" \
  --raw "$output_dir/optimized_correct/${name}_optimized.raw" --target ZHUGE \
  --qdtype bf16 --forward_dir "$calibration_dir" \
  --jr_path "$output_dir/quantized_correct" > "$output_dir/quantize.log" 2>&1
icraft-adapt --json "$output_dir/quantized_correct/${name}_quantized.json" \
  --raw "$output_dir/quantized_correct/${name}_quantized.raw" --target ZHUGE \
  --jr_path "$output_dir/adapted" > "$output_dir/adapt.log" 2>&1
if ! ldconfig -p | grep 'libicraft_zhuge_codegen\.so' > /dev/null; then
  printf '%s\n' 'Missing official libicraft_zhuge_codegen.so. Adapted JSON/RAW are intermediate artifacts, not deployable ZG330 models.' >&2
  exit 3
fi
icraft-generate --json "$output_dir/adapted/${name}_adapted.json" \
  --raw "$output_dir/adapted/${name}_adapted.raw" \
  --jr_path "$output_dir/generated" > "$output_dir/generate.log" 2>&1
printf '%s\n' "Generated files: $output_dir/generated (board verification still required)"
