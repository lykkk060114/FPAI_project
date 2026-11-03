#!/usr/bin/env python3
"""Fit and validate distance_m = scale_m_px / bbox_height_px + bias_m.

The calibration CSV must contain distance_m and bbox_height_px.  An optional
split column (fit/validation) makes it possible to report hold-out error
without fitting and validating on the same frames.
"""

from __future__ import annotations

import argparse
import csv
import json
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Union

import numpy as np


parser = argparse.ArgumentParser()
parser.add_argument("samples", type=Path,
                    help="CSV with distance_m,bbox_height_px columns")
parser.add_argument("--output", type=Path,
                    help="optional YAML snippet output")
parser.add_argument("--validation", type=Path,
                    help="optional second CSV used only for validation")
parser.add_argument("--report", type=Path,
                    help="optional JSON report output")
args = parser.parse_args()
started = time.perf_counter()

def read_samples(path: Path, split: Optional[str] = None) -> Tuple[List[float], List[float]]:
    distances: List[float] = []
    heights: List[float] = []
    with path.open(newline="", encoding="utf-8") as stream:
        for line, row in enumerate(csv.DictReader(stream), start=2):
            row_split = (row.get("split") or "fit").strip()
            if row_split not in ("fit", "validation"):
                raise SystemExit(f"{path}:{line}: split must be fit or validation")
            if split is not None and row_split != split:
                continue
            try:
                distance = float(row["distance_m"])
                height = float(row["bbox_height_px"])
            except (KeyError, TypeError, ValueError) as exc:
                raise SystemExit(f"{path}:{line}: invalid distance/height") from exc
            if np.isfinite(distance) and np.isfinite(height) and distance > 0 and height >= 1:
                distances.append(distance)
                heights.append(height)
            else:
                raise SystemExit(f"{path}:{line}: distance/height must be finite and positive")
    return distances, heights


distances, heights = read_samples(args.samples, "fit")

if len(distances) < 3:
    raise SystemExit("at least three valid samples are required")
if len(set(distances)) < 3:
    raise SystemExit("at least three distinct measured fit distances are required")

x = 1.0 / np.asarray(heights, dtype=np.float64)
y = np.asarray(distances, dtype=np.float64)
design = np.column_stack([x, np.ones_like(x)])
parameters, _, rank, _ = np.linalg.lstsq(design, y, rcond=None)
scale, bias = parameters
if rank < 2 or not np.isfinite(parameters).all() or scale <= 0:
    raise SystemExit("invalid calibration: constant heights or non-positive scale")
predicted = scale * x + bias
if np.any(predicted <= 0):
    raise SystemExit("invalid calibration: non-positive predicted fit distance")


def metrics(actual: np.ndarray, estimate: np.ndarray) -> Dict[str, Union[float, int]]:
    error = estimate - actual
    return {
        "samples": int(actual.size),
        "mae_m": float(np.mean(np.abs(error))),
        "rmse_m": float(np.sqrt(np.mean(error ** 2))),
        "max_abs_error_m": float(np.max(np.abs(error))),
        "median_abs_error_m": float(np.median(np.abs(error))),
    }


fit_metrics = metrics(y, predicted)
ss_total = float(np.sum((y - y.mean()) ** 2))
r_squared = 1.0 - float(np.sum((predicted - y) ** 2)) / ss_total \
    if ss_total > 0 else 1.0
fit_metrics["r2"] = float(r_squared)

validation_metrics: Optional[Dict[str, Union[float, int]]] = None
if args.validation:
    val_distances, val_heights = read_samples(args.validation)
    if not val_distances:
        raise SystemExit("validation CSV has no valid samples")
else:
    val_distances, val_heights = read_samples(args.samples, "validation")
if val_distances:
    val_y = np.asarray(val_distances, dtype=np.float64)
    val_predicted = scale / np.asarray(val_heights, dtype=np.float64) + bias
    validation_metrics = metrics(val_y, val_predicted)

snippet = (
    "ranging:\n"
    "  enabled: true\n"
    f"  scale_m_px: {scale:.8f}\n"
    f"  bias_m: {bias:.8f}\n"
    f"  min_distance_m: {y.min():.8f}\n"
    f"  max_distance_m: {y.max():.8f}\n"
    "  smoothing_alpha: 0.35\n"
)
print("fit=" + json.dumps(fit_metrics, sort_keys=True))
if validation_metrics:
    print("validation=" + json.dumps(validation_metrics, sort_keys=True))
print(snippet, end="")
elapsed_ms = (time.perf_counter() - started) * 1000
print(f"calibration_wall_ms={elapsed_ms:.3f}")
if args.output:
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        f"# samples={len(y)} rmse_m={fit_metrics['rmse_m']:.6f} r2={r_squared:.6f}\n" +
        snippet,
        encoding="utf-8",
    )
if args.report:
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps({
        "model": "distance_m = scale_m_px / bbox_height_px + bias_m",
        "scale_m_px": float(scale),
        "bias_m": float(bias),
        "fit": fit_metrics,
        "validation": validation_metrics,
        "calibration_wall_ms": elapsed_ms,
        "distance_interval_m": [float(y.min()), float(y.max())],
        "warning": "Valid only for calibrated target size/pose and fixed camera optics; validate at held-out distances before deployment.",
    }, indent=2) + "\n", encoding="utf-8")
