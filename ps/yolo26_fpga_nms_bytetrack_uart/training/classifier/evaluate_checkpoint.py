#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path

import timm
import torch
from timm.layers import set_fused_attn
from torch.utils.data import DataLoader
from torchvision import datasets, transforms


parser = argparse.ArgumentParser()
parser.add_argument("checkpoint", type=Path)
parser.add_argument("--data", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
torch.set_num_threads(8)
set_fused_attn(False)

checkpoint = torch.load(args.checkpoint, map_location="cpu", weights_only=True)
classes = checkpoint["classes"]
model = timm.create_model(checkpoint["architecture"], pretrained=False,
                          num_classes=len(classes))
model.load_state_dict(checkpoint["model"])
model.eval()
transform = transforms.Compose([
    transforms.Resize((224, 224)),
    transforms.ToTensor(),
    transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225]),
])
dataset = datasets.ImageFolder(args.data / "val", transform)
if dataset.classes != classes:
    raise RuntimeError("checkpoint and validation class order differ")
loader = DataLoader(dataset, batch_size=32, shuffle=False, num_workers=4)
confusion = torch.zeros(len(classes), len(classes), dtype=torch.int64)
with torch.inference_mode():
    for images, labels in loader:
        predictions = model(images).argmax(dim=1)
        for actual, predicted in zip(labels, predictions):
            confusion[actual, predicted] += 1

per_class = {}
f1_values = []
for index, label in enumerate(classes):
    tp = confusion[index, index].item()
    fp = confusion[:, index].sum().item() - tp
    fn = confusion[index, :].sum().item() - tp
    precision = tp / (tp + fp) if tp + fp else 0.0
    recall = tp / (tp + fn) if tp + fn else 0.0
    f1 = 2 * precision * recall / (precision + recall) \
        if precision + recall else 0.0
    f1_values.append(f1)
    per_class[label] = {"precision": precision, "recall": recall, "f1": f1,
                        "support": int(confusion[index, :].sum())}

report = {
    "checkpoint_epoch": checkpoint["epoch"],
    "accuracy": confusion.diag().sum().item() / confusion.sum().item(),
    "macro_f1": sum(f1_values) / len(f1_values),
    "classes": classes,
    "confusion_matrix_rows_actual_cols_predicted": confusion.tolist(),
    "per_class": per_class,
}
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(json.dumps(report, indent=2))
