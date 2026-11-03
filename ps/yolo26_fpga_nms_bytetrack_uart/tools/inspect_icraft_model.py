#!/usr/bin/env python3
"""Print the op IDs needed to configure an iCraft JSON/RAW artifact."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


parser = argparse.ArgumentParser()
parser.add_argument("json_model", type=Path)
args = parser.parse_args()
network = json.loads(args.json_model.read_text(encoding="utf-8"))

print(f"name={network.get('name')}")
print(f"icraft_version={network.get('icraft_version')}")
print(f"ai_target={network.get('ai_target')}")
print(f"params_bytes={network.get('params_bytes')}")
print("index\top_id\ttype\tname")
for index, operation in enumerate(network.get("ops", [])):
    type_key = operation.get("_type_key", "")
    name = operation.get("name", "")
    if (
        index < 8
        or index >= len(network["ops"]) - 8
        or any(
            key in f"{type_key} {name}"
            for key in ("Input", "Output", "Cast", "Reshape", "DetPost", "TopK")
        )
    ):
        print(f"{index}\t{operation.get('op_id')}\t{type_key}\t{name}")
