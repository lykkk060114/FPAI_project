#!/usr/bin/env python3
"""Read one RGB565 SDI frame from an already-running u-dma-buf capture pipeline."""
from __future__ import annotations

import argparse
import mmap
from pathlib import Path

import cv2
import numpy as np


def parse_int(value: str) -> int:
    return int(value, 0)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", default="/dev/udmabuf0")
    parser.add_argument("--region-phys", type=parse_int, required=True,
                        help="physical base address of the u-dma-buf region")
    parser.add_argument("--frame-phys", type=parse_int, required=True,
                        help="physical address printed by the running camera program")
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    frame_bytes = args.width * args.height * 2
    offset = args.frame_phys - args.region_phys
    if offset < 0:
        raise ValueError(f"frame offset must be non-negative: {offset:#x}")
    map_offset = offset & ~(mmap.PAGESIZE - 1)
    frame_offset = offset - map_offset

    # The camera program owns the buffer and continues to run. This is a read-only snapshot.
    with open(args.device, "rb", buffering=0) as dma:
        mapped = mmap.mmap(dma.fileno(), frame_offset + frame_bytes,
                           access=mmap.ACCESS_READ, offset=map_offset)
        try:
            rgb565 = np.frombuffer(mapped, dtype=np.uint8, count=frame_bytes,
                                   offset=frame_offset).copy().reshape(args.height, args.width, 2)
        finally:
            mapped.close()

    bgr = cv2.cvtColor(rgb565, cv2.COLOR_BGR5652BGR)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not cv2.imwrite(str(args.output), bgr):
        raise RuntimeError(f"could not write {args.output}")
    print(f"saved={args.output} shape={bgr.shape} bytes={frame_bytes} offset={offset:#x}")


if __name__ == "__main__":
    main()
