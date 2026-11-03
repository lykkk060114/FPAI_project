#!/usr/bin/env python3
"""Export a trained Ultralytics YOLO26 detector for the FMQL pipeline."""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path

import onnx
from ultralytics import YOLO


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("weights", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--height", type=int, default=352)
    parser.add_argument("--width", type=int, default=640)
    parser.add_argument("--opset", type=int, default=13)
    parser.add_argument(
        "--traditional-head", action="store_true",
        help="export [1,4+nc,N] and use external NMS instead of [1,300,6]",
    )
    return parser.parse_args()


def output_shape(model: onnx.ModelProto) -> list[int | str | None]:
    result: list[int | str | None] = []
    for dimension in model.graph.output[0].type.tensor_type.shape.dim:
        if dimension.HasField("dim_value"):
            result.append(dimension.dim_value)
        elif dimension.HasField("dim_param"):
            result.append(dimension.dim_param)
        else:
            result.append(None)
    return result


def main() -> None:
    args = parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    detector = YOLO(str(args.weights))
    exported = Path(detector.export(
        format="onnx",
        imgsz=(args.height, args.width),
        batch=1,
        dynamic=False,
        simplify=False,
        opset=args.opset,
        nms=None if args.traditional_head else False,
        end2end=False if args.traditional_head else True,
        device="cpu",
    ))
    destination = args.output / "yolo26_panda_352x640.onnx"
    shutil.copy2(exported, destination)
    model = onnx.load(destination)
    onnx.checker.check_model(model)
    shape = output_shape(model)
    if not args.traditional_head and shape != [1, 300, 6]:
        raise RuntimeError(f"expected end-to-end [1,300,6], got {shape}")
    metadata = {
        "source_weights": str(args.weights.resolve()),
        "onnx": str(destination.resolve()),
        "input_shape": [1, 3, args.height, args.width],
        "output_shape": shape,
        "head": "one-to-many" if args.traditional_head else "one-to-one",
        "postprocess": "external class-aware NMS" if args.traditional_head
                       else "confidence filter only; NMS-free",
        "opset": args.opset,
        "names": detector.names,
    }
    (args.output / "yolo26_export.json").write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(metadata, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
