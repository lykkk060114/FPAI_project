#!/usr/bin/env python3
"""Compare generated ZG330 instruction simulation with the original ONNX."""
import argparse
import json
import subprocess
from pathlib import Path

import cv2
import numpy as np
import onnxruntime as ort

from yolo26_conv_decode import decode

parser = argparse.ArgumentParser()
parser.add_argument("conversion_dir", type=Path)
parser.add_argument("images", type=Path)
parser.add_argument("--container", default="fmql-windows-codegen-20261013")
parser.add_argument("--count", type=int, default=8)
args = parser.parse_args()
b = args.conversion_dir.resolve()
project = Path(__file__).resolve().parents[4]
bo = "/project/" + str(b.relative_to(project))
options = ort.SessionOptions()
options.intra_op_num_threads = 4
reference = ort.InferenceSession(str(b / "e2e/yolo26_panda_352x640.onnx"), options)
network = json.loads((b / "generated/yolo26_panda_conv_ZG.json").read_text())
outputs = network["ops"][-1]["inputs"]
paths = sorted(p for p in args.images.iterdir() if p.suffix.lower() in (".jpg", ".jpeg", ".png"))
paths = [paths[i] for i in np.linspace(0, len(paths) - 1, min(args.count, len(paths))).astype(int)]
records = []
for index, path in enumerate(paths):
    im = cv2.imread(str(path))
    h, w = im.shape[:2]
    scale = min(640 / w, 352 / h)
    nw, nh = round(w * scale), round(h * scale)
    im = cv2.resize(im, (nw, nh))
    left, top = (640 - nw) // 2, (352 - nh) // 2
    im = cv2.copyMakeBorder(im, top, 352 - nh - top, left, 640 - nw - left,
                           cv2.BORDER_CONSTANT, value=(114, 114, 114))
    case = b / "validation_sim_bgr_input" / f"{index:02}"
    case.mkdir(parents=True, exist_ok=True)
    # icraft-run's image loader decodes RGB. The generated model expects BGR bytes.
    cv2.imwrite(str(case / "input.png"), im[:, :, ::-1])
    co = "/project/" + str(case.relative_to(project))
    program = r'''
set -euo pipefail
export WINEPREFIX=/tmp/fmql-wine WINEDEBUG=-all
export WINEDLLOVERRIDES="msvcp140,msvcp140_1,msvcp140_2,vcruntime140,vcruntime140_1,vcomp140,concrt140=n,b"
b="$1"; c="$2"; win="Z:${b//\//\\}"; image="Z:${c//\//\\}"
xvfb-run -a /usr/lib/wine/wine64 /tmp/icraft-win-bin/icraft-run.exe --json "$win\\generated\\yolo26_panda_conv_ZG.json" --raw "$win\\generated\\yolo26_panda_conv_ZG.raw" --input "$image\\input.png" --backends Host --dump_output --dump_format SFB --dump_ftmp "$3" --log_path "$image\\dump" > "$c/run.log" 2>&1
'''
    ids = ",".join(str(v["v_id"]) for v in outputs)
    subprocess.run(["docker", "exec", args.container, "bash", "-c", program, "_", bo, co, ids], check=True)
    tensors = [np.fromfile(case / f"dump/yolo26_panda_conv/ftmpSFB/{v['v_id']}.ftmp", np.float32)
               .reshape(v["dtype"]["shape"]) for v in outputs]
    actual = decode(tensors, layout="NHWC")
    x = np.ascontiguousarray(im[:, :, ::-1].transpose(2, 0, 1)[None]).astype(np.float32) / 255
    expected = reference.run(None, {reference.get_inputs()[0].name: x})[0][0]
    expected = expected[expected[:, 4] >= 0.5]
    matches = []
    for row in expected:
        values = []
        for other in actual:
            intersection = np.prod(np.maximum(0, np.minimum(row[2:4], other[2:4]) - np.maximum(row[:2], other[:2])))
            union = np.prod(row[2:4] - row[:2]) + np.prod(other[2:4] - other[:2]) - intersection
            values.append(float(intersection / union) if union > 0 else 0.0)
        matches.append(max(values, default=0.0))
    records.append({"image": str(path), "generated": actual.tolist(), "reference": expected.tolist(),
                    "equal_count": len(actual) == len(expected), "matched_iou": matches})
    print(index, len(actual), len(expected), matches, flush=True)
report = {"scope": "ZG330 instruction simulation; BGR input aligned; no physical board test",
          "cases": records, "equal_counts": sum(r["equal_count"] for r in records),
          "min_matched_iou": min((v for r in records for v in r["matched_iou"]), default=None)}
(b / "generated_alignment.json").write_text(json.dumps(report, indent=2) + "\n")
