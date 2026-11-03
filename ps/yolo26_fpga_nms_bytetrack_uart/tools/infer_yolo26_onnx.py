#!/usr/bin/env python3
"""Original end-to-end YOLO26 ONNX inference; saves the exact RGB input."""
import argparse
import json
from pathlib import Path

import cv2
import numpy as np
import onnxruntime as ort


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--model", type=Path, default=root / "training/detection/output/panda_yolo26s-2/yolo26_panda_352x640.onnx")
    parser.add_argument("--mode", choices=("letterbox", "pl-crop", "ready"), default="letterbox",
                        help="ready: already processed 640x352; pl-crop: assumed FPGA sample phase")
    parser.add_argument("--swap-rb", action="store_true", help="Diagnostic: exchange red/blue before inference")
    parser.add_argument("--confidence", type=float, default=0.5)
    parser.add_argument("--output", type=Path, default=root / "results/onnx_check")
    args = parser.parse_args()
    if not 0 <= args.confidence <= 1:
        parser.error("confidence must be between 0 and 1")
    image = cv2.imread(str(args.image))
    if image is None:
        parser.error(f"Cannot read image: {args.image}")
    options = ort.SessionOptions()
    options.intra_op_num_threads = 4
    session = ort.InferenceSession(str(args.model), options, providers=["CPUExecutionProvider"])
    spec = session.get_inputs()[0]
    if spec.type != "tensor(float)" or spec.shape != [1, 3, 352, 640]:
        parser.error(f"Expected FP32 NCHW [1,3,352,640], got {spec.type} {spec.shape}")
    h, w = image.shape[:2]
    if args.mode == "letterbox":
        scale = min(640 / w, 352 / h)
        nw, nh = round(w * scale), round(h * scale)
        left, top = (640 - nw) // 2, (352 - nh) // 2
        prepared = cv2.copyMakeBorder(cv2.resize(image, (nw, nh)), top, 352 - nh - top,
                                     left, 640 - nw - left, cv2.BORDER_CONSTANT, value=(114, 114, 114))
        sx, sy, bx, by = w / nw, h / nh, -left * w / nw, -top * h / nh
    elif args.mode == "pl-crop":
        if (w, h) != (1920, 1080):
            parser.error("pl-crop requires a 1920x1080 original frame")
        prepared = image[12:1068:3, 0:1920:3].copy()
        sx, sy, bx, by = 3, 3, 0, 12
    else:
        if (w, h) != (640, 352):
            parser.error("ready requires a 640x352 preprocessed image")
        prepared = image.copy()
        sx, sy, bx, by = 1, 1, 0, 0
    rgb = cv2.cvtColor(prepared, cv2.COLOR_BGR2RGB)
    if args.swap_rb:
        rgb = rgb[:, :, ::-1].copy()
    tensor = np.ascontiguousarray(rgb.transpose(2, 0, 1)[None], dtype=np.float32) / 255.0
    outputs = session.run(None, {spec.name: tensor})
    if len(outputs) != 1 or outputs[0].ndim != 3 or outputs[0].shape[0] != 1 or outputs[0].shape[2] != 6:
        parser.error(f"Expected end-to-end [1,N,6] output, got {[a.shape for a in outputs]}")
    rows = outputs[0][0]
    valid = np.isfinite(rows).all(axis=1)
    selected = rows[valid & (rows[:, 4] >= args.confidence)].copy()
    annotated = image.copy()
    mapped = []
    for x1, y1, x2, y2, score, cls in selected:
        box = np.array([x1*sx+bx, y1*sy+by, x2*sx+bx, y2*sy+by])
        box[[0, 2]] = np.clip(box[[0, 2]], 0, w-1)
        box[[1, 3]] = np.clip(box[[1, 3]], 0, h-1)
        a, b, c, d = box.round().astype(int)
        cv2.rectangle(annotated, (a, b), (c, d), (0, 255, 0), 2)
        cv2.putText(annotated, f"panda {score:.3f}", (a, max(15, b-5)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)
        mapped.append({"box_xyxy": box.tolist(), "confidence": float(score), "class_id": int(cls)})
    args.output.mkdir(parents=True, exist_ok=True)
    # imwrite expects BGR; PNG displays the actual RGB values given to the network.
    if not cv2.imwrite(str(args.output / "model_input.png"), cv2.cvtColor(rgb, cv2.COLOR_RGB2BGR)):
        raise RuntimeError("Failed to save model input")
    if not cv2.imwrite(str(args.output / "detections.png"), annotated):
        raise RuntimeError("Failed to save detections")
    np.save(args.output / "model_input_nchw.npy", tensor)
    np.save(args.output / "onnx_output.npy", outputs[0])
    report = {"model": str(args.model.resolve()), "image": str(args.image.resolve()), "mode": args.mode,
              "swap_rb": args.swap_rb, "input_shape": list(tensor.shape), "input_range": [float(tensor.min()), float(tensor.max())],
              "maximum_confidence": float(rows[valid, 4].max()) if valid.any() else None,
              "confidence_threshold": args.confidence, "detections": mapped,
              "sampling_note": "pl-crop assumes sample at x=3*i,y=12+3*j; verify RTL phase"}
    (args.output / "result.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({"detections": len(mapped), "maximum_confidence": report["maximum_confidence"],
                      "output": str(args.output.resolve())}, ensure_ascii=False))


if __name__ == "__main__":
    main()
