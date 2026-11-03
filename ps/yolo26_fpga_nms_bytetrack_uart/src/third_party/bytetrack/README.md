# ByteTrack C++ core

This directory vendors the tracking-only C++ implementation from the official
[FoundationVision/ByteTrack](https://github.com/FoundationVision/ByteTrack)
repository.

- Upstream commit: `d1bf0191adff59bc8fcfeaa0b33d3d1642552a99`
- Upstream path: `deploy/ncnn/cpp`
- License: MIT, preserved in `LICENSE`
- Vendored files: `BYTETracker`, `STrack`, Kalman filter, and LAPJV sources

The ncnn detector demo is intentionally excluded. Detection remains in the
FMQL30TAI FPAI YOLO26 pipeline; this code only associates detection boxes.
Project-specific filtering, target locking, coordinate clamping, and UART
publishing live outside this directory.
