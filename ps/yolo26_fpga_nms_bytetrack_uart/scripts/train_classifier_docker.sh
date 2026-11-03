#!/usr/bin/env bash
set -euo pipefail

package_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
data_dir="${1:-/home/lykkk/work_space/fmql/datasets/classification}"
output_dir="${OUTPUT_DIR:-$package_dir/training/classifier/output}"
image="xunjing-deit-train:2.5.1"

mkdir -p "$output_dir"
docker build -t "$image" "$package_dir/training/classifier"
docker run --rm --gpus all --ipc=host \
  -v "$package_dir/training/classifier:/workspace:ro" \
  -v "$data_dir:/data:ro" \
  -v "$output_dir:/output" \
  "$image" \
  python /workspace/train.py \
    --data /data \
    --output /output \
    --epochs "${EPOCHS:-40}" \
    --batch-size "${BATCH_SIZE:-32}" \
    --workers "${WORKERS:-8}"

docker run --rm \
  -v "$package_dir/training/classifier:/workspace:ro" \
  -v "$output_dir:/output:ro" \
  "$image" \
  python /workspace/verify_onnx.py /output/deit_tiny_panda_behavior.onnx \
    --checkpoint /output/deit_tiny_panda_behavior_best.pt

docker run --rm --ipc=host \
  -v "$package_dir/training/classifier:/workspace:ro" \
  -v "$data_dir:/data:ro" \
  -v "$output_dir:/output" \
  "$image" python /workspace/evaluate_checkpoint.py \
    /output/deit_tiny_panda_behavior_best.pt --data /data \
    --output /output/deit_tiny_panda_behavior_evaluation.json

mkdir -p "$package_dir/imodel/classifier"
cp "$output_dir/deit_tiny_panda_behavior.onnx" \
   "$package_dir/imodel/classifier/deit_tiny_panda_behavior.onnx"
cp "$output_dir/deit_tiny_panda_behavior.json" \
   "$package_dir/imodel/classifier/deit_tiny_panda_behavior.json"
cp "$output_dir/deit_tiny_panda_behavior_evaluation.json" \
   "$package_dir/imodel/classifier/deit_tiny_panda_behavior_evaluation.json"
python3 - "$output_dir/deit_tiny_panda_behavior.json" "$package_dir/names/panda_behavior.names" <<'PY'
import json, pathlib, sys
classes = json.loads(pathlib.Path(sys.argv[1]).read_text())["classes"]
pathlib.Path(sys.argv[2]).write_text("\n".join(classes) + "\n")
PY
# Refresh hashes after replacing trained weights, retaining all existing entries.
(cd "$package_dir"
  { awk '{print $2}' SHA256SUMS
    printf '%s\n' imodel/classifier/deit_tiny_panda_behavior.{onnx,json} \
      imodel/classifier/deit_tiny_panda_behavior_evaluation.json names/panda_behavior.names
  } | sort -u | while IFS= read -r path; do
    [[ ! -f "$path" ]] || sha256sum "$path"
  done > SHA256SUMS.new
  mv SHA256SUMS.new SHA256SUMS)
echo "Classifier artifacts copied to $package_dir/imodel/classifier"
