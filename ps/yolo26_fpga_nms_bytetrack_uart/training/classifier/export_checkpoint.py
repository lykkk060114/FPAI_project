#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

import timm
import torch
from timm.layers import set_fused_attn

from train import export_onnx


parser = argparse.ArgumentParser()
parser.add_argument("checkpoint", type=Path)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()

set_fused_attn(False)
checkpoint = torch.load(args.checkpoint, map_location="cpu", weights_only=True)
model = timm.create_model(checkpoint["architecture"], pretrained=False,
                          num_classes=len(checkpoint["classes"]))
model.load_state_dict(checkpoint["model"])
args.output.parent.mkdir(parents=True, exist_ok=True)
export_onnx(model, args.output, checkpoint["classes"], checkpoint["architecture"])
print(args.output)
