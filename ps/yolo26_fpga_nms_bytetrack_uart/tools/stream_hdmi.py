#!/usr/bin/env python3
"""Read-only RGB565 HDMI snapshots, fast LZ4 transport with JPEG fallback."""
import argparse
import contextlib
import ctypes
import fcntl
import mmap
import os
from pathlib import Path
import signal
import struct
import sys
import time

import cv2
import numpy as np

# Verified in modelzoo_utils/pipeline/io/output/hdmi_display.hpp:
# show() writes display_chunk_->begin.addr() >> 3 to DISPLAY_READ_ADDR.
DISPLAY_READ_ADDR = 0x40080054
SOURCE_W, SOURCE_H = 1920, 1080
PACKET = struct.Struct("<4sIddd")  # magic, JPEG bytes, monotonic s, read ms, encode ms


class Lz4Codec:
    """Use the system LZ4 library; avoid slow board-side JPEG encoding."""
    def __init__(self):
        self.lib = ctypes.CDLL('liblz4.so.1')
        self.lib.LZ4_compressBound.argtypes = [ctypes.c_int]
        self.lib.LZ4_compressBound.restype = ctypes.c_int
        self.lib.LZ4_compress_fast.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                              ctypes.c_int, ctypes.c_int, ctypes.c_int]
        self.lib.LZ4_compress_fast.restype = ctypes.c_int
        self.lib.LZ4_decompress_safe.argtypes = [ctypes.c_char_p, ctypes.c_void_p,
                                                ctypes.c_int, ctypes.c_int]
        self.lib.LZ4_decompress_safe.restype = ctypes.c_int
        self.output_buffer = None
        self.output_capacity = 0

    def compress(self, image):
        image = np.ascontiguousarray(image)
        bound = self.lib.LZ4_compressBound(image.nbytes)
        if bound > self.output_capacity:
            self.output_buffer = ctypes.create_string_buffer(bound)
            self.output_capacity = bound
        size = self.lib.LZ4_compress_fast(image.ctypes.data, self.output_buffer,
                                         image.nbytes, self.output_capacity, 8)
        if size <= 0:
            raise RuntimeError('LZ4 compression failed')
        height, width = image.shape[:2]
        return struct.pack('<HH', width, height) + ctypes.string_at(self.output_buffer, size)

    def decode(self, payload):
        if len(payload) < 5:
            raise RuntimeError('Truncated LZ4 image')
        width, height = struct.unpack_from('<HH', payload)
        if not 1 <= width <= SOURCE_W or not 1 <= height <= SOURCE_H:
            raise RuntimeError('Invalid LZ4 image dimensions')
        expected = width * height * 2
        out = ctypes.create_string_buffer(expected)
        compressed = payload[4:]
        size = self.lib.LZ4_decompress_safe(compressed, out, len(compressed), expected)
        if size != expected:
            raise RuntimeError('Corrupt LZ4 image')
        raw = np.frombuffer(out.raw, np.uint8).reshape(height, width, 2)
        return cv2.cvtColor(raw, cv2.COLOR_BGR5652BGR)


class HdmiReader:
    def __init__(self):
        self.resources = contextlib.ExitStack()
        try:
            root = Path('/sys/class/u-dma-buf/udmabuf0')
            self.base = int((root / 'phys_addr').read_text().strip(), 0)
            size = int((root / 'size').read_text().strip(), 0)
            self.reg = self.map_readonly('/dev/mem', mmap.PAGESIZE,
                                         DISPLAY_READ_ADDR & ~(mmap.PAGESIZE - 1))
            self.reg_offset = DISPLAY_READ_ADDR & (mmap.PAGESIZE - 1)
            self.dma = self.map_readonly('/dev/udmabuf0', size, 0)
            self.size = size
        except BaseException:
            self.resources.close()
            raise

    def map_readonly(self, path, length, offset):
        # CPU writes the display image. Use the normal cached u-dma-buf mapping,
        # as in capture_udmabuf_frame.py; keep MMIO registers uncached.
        fd = os.open(path, os.O_RDONLY | (os.O_SYNC if path == '/dev/mem' else 0))
        try:
            region = mmap.mmap(fd, length, flags=mmap.MAP_SHARED,
                               prot=mmap.PROT_READ, offset=offset)
        finally:
            os.close(fd)
        return self.resources.enter_context(region)

    def read(self):
        # No CSR writes, cache synchronization or changes to application ownership.
        # This is a display snapshot, not an exclusive same-frame application tap.
        for _ in range(3):
            address = struct.unpack_from('<I', self.reg, self.reg_offset)[0] << 3
            offset = address - self.base
            length = SOURCE_W * SOURCE_H * 2
            if offset < 0 or offset + length > self.size:
                raise RuntimeError('HDMI 地址不在 udmabuf0 内，请先启动巡境检测程序')
            first = self.dma[offset:offset + length]
            second = self.dma[offset:offset + length]
            after = struct.unpack_from('<I', self.reg, self.reg_offset)[0] << 3
            if address == after and first == second:
                return np.frombuffer(second, dtype=np.uint8).reshape(SOURCE_H, SOURCE_W, 2)
        return None  # Display buffer changed while copying; drop rather than queue.

    def close(self):
        self.resources.close()


def stream(reader, sink, fps, width, quality, seconds=0):
    cv2.setNumThreads(1)
    try:
        codec = Lz4Codec()
    except OSError:
        codec = None
    height = round(width * SOURCE_H / SOURCE_W / 2) * 2
    started = time.monotonic()
    deadline = started
    sent = 0
    cpu_started = time.process_time()
    read_total = encode_total = 0.0
    while not seconds or time.monotonic() - started < seconds:
        delay = deadline - time.monotonic()
        if delay > 0:
            time.sleep(delay)
        before = time.monotonic()
        image = reader.read()
        captured = time.monotonic()
        if image is not None:
            if width != SOURCE_W:
                image = cv2.resize(image, (width, height), interpolation=cv2.INTER_NEAREST)
            if codec is not None:
                encoded = codec.compress(image)
                magic = b'XJL4'
            else:
                image = cv2.cvtColor(image, cv2.COLOR_BGR5652BGR)
                ok, jpeg = cv2.imencode('.jpg', image, [cv2.IMWRITE_JPEG_QUALITY, quality])
                if not ok:
                    raise RuntimeError('JPEG 编码失败')
                encoded = jpeg.tobytes()
                magic = b'XJCP'
            done = time.monotonic()
            read_ms = (captured - before) * 1000
            encode_ms = (done - captured) * 1000
            sink.write(PACKET.pack(magic, len(encoded), captured, read_ms, encode_ms))
            sink.write(encoded)
            sink.flush()
            sent += 1
            read_total += read_ms
            encode_total += encode_ms
        deadline += 1.0 / fps
        if deadline < time.monotonic():
            deadline = time.monotonic()
    return {'frames': sent, 'elapsed_s': time.monotonic() - started,
            'cpu_s': time.process_time() - cpu_started,
            'read_ms_mean': read_total / max(sent, 1),
            'encode_ms_mean': encode_total / max(sent, 1)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fps', type=int, default=20)
    parser.add_argument('--width', type=int, default=1280)
    parser.add_argument('--quality', type=int, default=85)
    parser.add_argument('--seconds', type=float, default=0)
    args = parser.parse_args()
    if not 1 <= args.fps <= 30 or not 320 <= args.width <= 1920 or args.width % 2:
        parser.error('fps=1..30, width=320..1920 and even')
    if not 1 <= args.quality <= 100 or args.seconds < 0:
        parser.error('quality=1..100, seconds>=0')
    lock = open('/tmp/xunjing_hdmi_capture.lock', 'a')
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        raise RuntimeError('板端已经有录屏在运行')
    active = False
    for path in Path('/proc').glob('[0-9]*/cmdline'):
        try:
            executable = os.path.basename(path.read_bytes().split(b'\0', 1)[0])
            if executable.startswith(b'sdicamera+yolo26'):
                active = True
                break
        except (FileNotFoundError, PermissionError):
            pass
    if not active:
        raise RuntimeError('巡境 YOLO26 检测程序未运行，请先执行 run')
    reader = HdmiReader()
    try:
        stream(reader, sys.stdout.buffer, args.fps, args.width, args.quality, args.seconds)
    finally:
        reader.close()


if __name__ == '__main__':
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
    try:
        main()
    except BrokenPipeError:
        os._exit(0)  # SSH disconnected: release mappings/lock on process exit.
    except Exception as exc:
        print(str(exc), file=sys.stderr, flush=True)
        sys.exit(1)
