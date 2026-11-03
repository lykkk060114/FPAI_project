#!/usr/bin/env python3
"""Compare ONNX inference on dataset originals and simulated 1080p PL sampling.

This tests crop/sampling geometry, NOT ImageMake signed bytes or NPU memory layout.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

import cv2
import numpy as np


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("images", type=Path)
    parser.add_argument("--count", type=int, default=10)
    parser.add_argument("--output", type=Path, default=root / "results/dataset_pl_preprocess")
    args = parser.parse_args()
    paths = sorted(p for p in args.images.iterdir() if p.suffix.lower() in (".jpg", ".png", ".jpeg"))
    if not paths or args.count < 1:
        parser.error("Need images and a positive count")
    paths = [paths[i] for i in np.linspace(0, len(paths)-1, min(args.count, len(paths))).astype(int)]
    records = []
    for index, path in enumerate(paths):
        image = cv2.imread(str(path))
        if image is None:
            raise RuntimeError(f"Cannot read {path}")
        h, w = image.shape[:2]
        ratio = min(1920 / w, 1080 / h)
        nw, nh = round(w * ratio), round(h * ratio)
        canvas = np.full((1080, 1920, 3), 114, np.uint8)
        x, y = (1920 - nw)//2, (1080 - nh)//2
        canvas[y:y+nh, x:x+nw] = cv2.resize(image, (nw, nh))
        case = args.output / f"{index:02}_{path.stem}"
        case.mkdir(parents=True, exist_ok=True)
        camera = case / "simulated_camera_1920x1080.png"
        if not cv2.imwrite(str(camera), canvas):
            raise RuntimeError("Cannot save simulated frame")
        entry = {"source": str(path.resolve())}
        for name, source, mode in (("original", path, "letterbox"),
                                   ("camera_letterbox", camera, "letterbox"),
                                   ("camera_pl_crop", camera, "pl-crop")):
            out = case / name
            subprocess.run([sys.executable, str(root / "tools/infer_yolo26_onnx.py"),
                            str(source), "--mode", mode, "--output", str(out)], check=True,
                           stdout=subprocess.DEVNULL)
            result = json.loads((out / "result.json").read_text())
            entry[name] = {"count": len(result["detections"]), "max_confidence": result["maximum_confidence"]}
        records.append(entry)
        print(json.dumps(entry), flush=True)
    report = {"scope": "CPU geometry simulation + original ONNX; not actual FPGA/NPU execution",
              "camera_simulation": "aspect-preserving fit into gray114 1920x1080 canvas",
              "pl_sampling": "out[y,x] = in[12+3*y,3*x]; RTL sampling phase not verified",
              "threshold": 0.5, "cases": records}
    (args.output / "summary.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
