#!/usr/bin/env python3
"""Collect only new locked-target rows from the running application's timing CSV.

Without --distance this checks stability only and never creates calibration data.
Each measured position contributes one median height, not many correlated frames.
"""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics
import time


def collect(path, seconds):
    rows, rejected = [], 0
    start = time.monotonic()
    with path.open(newline="") as stream:
        fields = next(csv.reader([stream.readline()]))
        required = {"steady_ns", "track_id", "bbox_height_px", "center_y", "detections"}
        if not required.issubset(fields):
            raise ValueError("timing CSV lacks locked-target fields")
        # Reject a partial row at the seek point, and keep partial new rows pending.
        stream.seek(0, 2)
        position = stream.tell()
        skip_partial = False
        if position:
            stream.seek(position - 1)
            skip_partial = stream.read(1) != "\n"
            stream.seek(position)
        pending = ""
        last_stamp = -1
        while time.monotonic() - start < seconds:
            chunk = stream.read()
            if not chunk:
                time.sleep(0.02)
                continue
            pending += chunk
            while "\n" in pending:
                line, pending = pending.split("\n", 1)
                if skip_partial:
                    skip_partial = False
                    continue
                try:
                    values = next(csv.reader([line]))
                    if len(values) != len(fields):
                        raise ValueError("partial/malformed row")
                    row = dict(zip(fields, values))
                    stamp = int(row["steady_ns"])
                    height, cy = float(row["bbox_height_px"]), float(row["center_y"])
                    if (int(row["track_id"]) < 0 or int(row["detections"]) < 1
                            or stamp <= last_stamp or not math.isfinite(height)
                            or not math.isfinite(cy) or height < 10
                            or cy - height / 2 <= 2 or cy + height / 2 >= 1078):
                        raise ValueError("missing, stale or vertically clipped target")
                    last_stamp = stamp
                    rows.append(row)
                except (ValueError, KeyError, TypeError, StopIteration):
                    rejected += 1
    return rows, rejected, time.monotonic() - start


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timing", required=True, type=Path,
                        help="CSV currently opened by the live process, not an old file")
    parser.add_argument("--seconds", type=float, default=8)
    parser.add_argument("--distance", type=float, help="measured lens-to-image-plane metres")
    parser.add_argument("--output", type=Path, help="append one position to calibration CSV")
    parser.add_argument("--evidence", required=True, type=Path,
                        help="unique JSON filename; raw rows saved beside it")
    parser.add_argument("--split", choices=["fit", "validation"], default="fit")
    parser.add_argument("--min-frames", type=int, default=30)
    parser.add_argument("--max-cv", type=float, default=0.03)
    args = parser.parse_args()
    if (not math.isfinite(args.seconds) or args.seconds <= 0 or args.min_frames < 2
            or not math.isfinite(args.max_cv) or args.max_cv <= 0):
        parser.error("invalid sampling duration or quality thresholds")
    if args.distance is not None and (not math.isfinite(args.distance) or args.distance <= 0 or not args.output):
        parser.error("measured --distance must be positive and requires --output")
    if args.output and args.distance is None:
        parser.error("--output requires an actual measured --distance")
    raw_path = args.evidence.with_suffix(".frames.csv")
    if args.evidence.exists() or raw_path.exists():
        parser.error("choose a new evidence filename")
    rows, rejected, elapsed = collect(args.timing, args.seconds)
    heights = [float(r["bbox_height_px"]) for r in rows]
    ids = sorted({int(r["track_id"]) for r in rows})
    median = statistics.median(heights) if heights else None
    cv = statistics.pstdev(heights) / statistics.mean(heights) if heights else None
    reasons = []
    if len(rows) < args.min_frames:
        reasons.append("too few fresh target frames")
    if len(ids) != 1:
        reasons.append("target absent or track ID changed")
    if cv is not None and cv > args.max_cv:
        reasons.append("box height unstable; keep target/camera still")
    report = dict(timing_csv=str(args.timing), elapsed_s=elapsed, valid_frames=len(rows),
                  rejected_frames=rejected, track_ids=ids, median_height_px=median,
                  height_cv=cv, distance_m=args.distance, split=args.split,
                  status="rejected" if reasons else ("measured" if args.distance is not None else "probe_only"),
                  reasons=reasons, frame_height=1080,
                  note="stability screening does not prove correct detection or metric distance")
    args.evidence.parent.mkdir(parents=True, exist_ok=True)
    with raw_path.open("w", newline="") as stream:
        if rows:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
    args.evidence.write_text(json.dumps(report, indent=2) + "\n")
    if not reasons and args.distance is not None:
        fields = ["distance_m", "bbox_height_px", "split", "frames", "height_cv", "evidence"]
        args.output.parent.mkdir(parents=True, exist_ok=True)
        exists = args.output.exists() and args.output.stat().st_size > 0
        if exists:
            with args.output.open(newline="") as stream:
                if next(csv.reader(stream)) != fields:
                    raise ValueError("output CSV header incompatible; use a fresh output file")
        with args.output.open("a", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields)
            if not exists:
                writer.writeheader()
            writer.writerow(dict(distance_m=args.distance, bbox_height_px=median,
                                 split=args.split, frames=len(rows), height_cv=cv,
                                 evidence=str(args.evidence)))
    print(json.dumps(report, indent=2))
    return 1 if reasons else 0


if __name__ == "__main__":
    raise SystemExit(main())
