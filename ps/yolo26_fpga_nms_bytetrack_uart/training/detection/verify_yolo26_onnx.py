#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort


parser = argparse.ArgumentParser()
parser.add_argument("model", type=Path)
args = parser.parse_args()

model = onnx.load(args.model)
onnx.checker.check_model(model)
session = ort.InferenceSession(str(args.model), providers=["CPUExecutionProvider"])
input_meta = session.get_inputs()[0]
shape = [int(value) for value in input_meta.shape]
outputs = session.run(None, {input_meta.name: np.zeros(shape, np.float32)})
if len(outputs) != 1 or outputs[0].ndim != 3 or not np.isfinite(outputs[0]).all():
    raise SystemExit(f"unexpected YOLO26 outputs: {[value.shape for value in outputs]}")
print(f"OK: input={shape}, output={list(outputs[0].shape)}")
