#!/usr/bin/env python3
"""Export the six one-to-one Conv outputs in the official YOLO26 tutorial order."""
import argparse
import json
from pathlib import Path

import onnx

parser = argparse.ArgumentParser()
parser.add_argument("model", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
model = onnx.load(args.model)
outputs = []
for scale in range(3):
    for branch in ("cv3", "cv2"):
        suffix = f"/one2one_{branch}.{scale}.2/Conv_output_0"
        matches = [value for node in model.graph.node if node.op_type == "Conv"
                   for value in node.output if value.endswith(suffix)]
        if len(matches) != 1:
            raise ValueError(f"Expected one output for {suffix}, found {matches}")
        outputs.extend(matches)
args.output.parent.mkdir(parents=True, exist_ok=True)
onnx.utils.extract_model(str(args.model), str(args.output),
                         [model.graph.input[0].name], outputs)
exported = onnx.load(args.output)
onnx.checker.check_model(exported)
metadata = {
    "head": "one-to-one",
    "input": [d.dim_value for d in exported.graph.input[0].type.tensor_type.shape.dim],
    "outputs": [{"name": o.name,
                 "shape": [d.dim_value for d in o.type.tensor_type.shape.dim]}
                for o in exported.graph.output],
    "output_order": "cls_stride8, box_stride8, cls_stride16, box_stride16, cls_stride32, box_stride32",
    "box_encoding": "ltrb distances from grid anchor (x+0.5,y+0.5), multiply by stride",
    "class_encoding": "logits; apply sigmoid",
    "postprocess": "top-300 confidence ranking, no NMS",
}
args.output.with_suffix(".json").write_text(json.dumps(metadata, indent=2) + "\n")
print(json.dumps(metadata, indent=2))
