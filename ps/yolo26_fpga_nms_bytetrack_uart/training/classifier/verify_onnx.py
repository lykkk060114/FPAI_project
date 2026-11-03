#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort


parser = argparse.ArgumentParser()
parser.add_argument("model", type=Path)
parser.add_argument("--checkpoint", type=Path)
args = parser.parse_args()

model = onnx.load(args.model)
onnx.checker.check_model(model)
options = ort.SessionOptions()
options.intra_op_num_threads = 2
session = ort.InferenceSession(str(args.model), sess_options=options, providers=["CPUExecutionProvider"])
input_meta = session.get_inputs()[0]
if input_meta.shape != [1, 3, 224, 224] or input_meta.type != "tensor(float)":
    raise SystemExit(f"unexpected classifier input: {input_meta}")
sample = np.random.default_rng(42).normal(size=(1, 3, 224, 224)).astype(np.float32)
output = session.run(None, {input_meta.name: sample})[0]
if output.shape != (1, 4) or not np.isfinite(output).all():
    raise SystemExit(f"unexpected classifier output: shape={output.shape}")
if args.checkpoint:
    import timm
    import torch
    from timm.layers import set_fused_attn
    torch.set_num_threads(2)
    set_fused_attn(False)
    checkpoint = torch.load(args.checkpoint, map_location="cpu", weights_only=True)
    reference = timm.create_model(checkpoint["architecture"], pretrained=False,
                                   num_classes=len(checkpoint["classes"]))
    reference.load_state_dict(checkpoint["model"])
    reference.eval()
    with torch.inference_mode():
        expected = reference(torch.from_numpy(sample)).numpy()
    np.testing.assert_allclose(output, expected, rtol=1e-3, atol=1e-4)
    print(f"PyTorch/ONNX parity OK, max error={np.max(np.abs(output - expected)):.6g}")
print(f"OK: input={input_meta.shape}, output={output.shape}")
