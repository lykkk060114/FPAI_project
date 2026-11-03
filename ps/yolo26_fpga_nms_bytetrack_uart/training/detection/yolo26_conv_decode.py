"""Decode the official six-output YOLO26 one-to-one head; no NMS."""
import numpy as np


def decode(outputs, confidence=0.5, max_det=300, layout="NCHW"):
    if len(outputs) != 6 or layout not in ("NCHW", "NHWC"):
        raise ValueError("Expected six tensors and NCHW/NHWC layout")
    boxes, scores = [], []
    for k, stride in enumerate((8, 16, 32)):
        cls, dist = outputs[2 * k:2 * k + 2]
        if layout == "NHWC":
            cls, dist = cls.transpose(0, 3, 1, 2), dist.transpose(0, 3, 1, 2)
        if cls.shape[:2] != (1, 1) or dist.shape[:2] != (1, 4) or cls.shape[2:] != dist.shape[2:]:
            raise ValueError("This decoder expects the single-class panda model")
        h, w = cls.shape[2:]
        grid = np.stack(np.meshgrid(np.arange(w), np.arange(h)), axis=0).reshape(2, -1) + 0.5
        d = dist.reshape(4, -1)
        boxes.append(np.concatenate((grid - d[:2], grid + d[2:]), axis=0).T * stride)
        scores.append(1 / (1 + np.exp(-np.clip(cls.reshape(-1), -80, 80))))
    boxes, scores = np.concatenate(boxes), np.concatenate(scores)
    indices = np.argsort(-scores, kind="stable")[:max_det]
    indices = indices[scores[indices] >= confidence]
    return np.column_stack((boxes[indices], scores[indices], np.zeros(len(indices))))
