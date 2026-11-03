#!/usr/bin/python3
"""Host command: start cap. Stream board HDMI snapshots over SSH to a local MP4."""
import argparse
from collections import deque
from datetime import datetime
import fcntl
import json
import os
from pathlib import Path
import selectors
import shlex
import signal
import struct
import subprocess
import sys
import time
from zoneinfo import ZoneInfo

import cv2
import numpy as np
from stream_hdmi import Lz4Codec

OUTPUT = Path('/home/lykkk/视频/capture')
PACKET = struct.Struct('<4sIddd')


class VideoSink:
    def __init__(self, fps, folder=OUTPUT):
        self.fps = fps
        self.folder = folder
        self.writer = self.path = self.previous = self.first_timestamp = None
        self.last_index = -1
        self.received = self.written = 0
        self.first_received = self.last_received = None
        self.read_total = self.encode_total = 0.0
        self.host_processing_s = 0.0
        self.codec = None
        self.transports = set()

    def add(self, jpeg, timestamp, read_ms, encode_ms, magic=b'XJCP'):
        before = time.monotonic()
        if magic == b'XJL4':
            if self.codec is None:
                self.codec = Lz4Codec()
            image = self.codec.decode(jpeg)
        else:
            image = cv2.imdecode(np.frombuffer(jpeg, np.uint8), cv2.IMREAD_COLOR)
        self.transports.add(magic.decode('ascii'))
        if image is None:
            raise RuntimeError('板端传来的 JPEG 无法解码')
        if self.writer is None:
            self.folder.mkdir(parents=True, exist_ok=True)
            stamp = datetime.now(ZoneInfo('Asia/Shanghai')).strftime('%Y-%m-%d_%H-%M')
            n = 1
            while True:
                suffix = '' if n == 1 else f'_{n:02d}'
                path = self.folder / f'{stamp}{suffix}.mp4'
                try:
                    path.touch(exist_ok=False)
                    self.path = path
                    break
                except FileExistsError:
                    n += 1
            h, w = image.shape[:2]
            self.shape = image.shape
            self.writer = cv2.VideoWriter(str(self.path), cv2.VideoWriter_fourcc(*'mp4v'),
                                          self.fps, (w, h))
            if not self.writer.isOpened():
                self.path.unlink()
                raise RuntimeError('MP4 编码器无法启动')
            self.first_timestamp = timestamp
            self.first_received = time.monotonic()
            print(f'正在录制：{self.path}\n{w}×{h}，目标 {self.fps} FPS；Ctrl+C 结束并保存。', flush=True)
        if image.shape != self.shape:
            raise RuntimeError('录制期间图像尺寸发生变化')
        index = round((timestamp - self.first_timestamp) * self.fps)
        if index < 0 or index - self.last_index > self.fps * 10:
            raise RuntimeError('图像时间戳发生跳变，已结束本次录制')
        # Match wall-clock duration; repeat the last snapshot across sampling gaps.
        while self.last_index + 1 < index:
            self.writer.write(self.previous)
            self.last_index += 1
            self.written += 1
        if index > self.last_index:
            self.writer.write(image)
            self.last_index = index
            self.written += 1
        self.previous = image
        self.received += 1
        self.last_received = timestamp
        self.read_total += read_ms
        self.encode_total += encode_ms
        self.host_processing_s += time.monotonic() - before

    def close(self, error=None, board=None):
        if self.writer is None:
            return None
        self.writer.release()
        seconds = max(self.last_received - self.first_timestamp, 0)
        data = {'source': 'board HDMI RGB565 display buffer snapshots over SSH',
                'board': board, 'video': str(self.path), 'requested_fps': self.fps,
                'received_frames': self.received, 'encoded_frames': self.written,
                'sample_span_s': seconds, 'video_duration_s': self.written / self.fps,
                'received_fps': (self.received - 1) / seconds if seconds else 0,
                'board_read_ms_mean': self.read_total / max(self.received, 1),
                'board_encode_ms_mean': self.encode_total / max(self.received, 1),
                'transport_codecs': sorted(self.transports),
                'host_decode_write_ms_mean': self.host_processing_s * 1000 / max(self.received, 1),
                'width': self.shape[1], 'height': self.shape[0], 'error': error,
                'note': 'Display snapshots; sampling FPS is not detector or HDMI refresh FPS. '
                        'Software overlays are included; downstream PL-only overlays are not captured.'}
        self.path.with_suffix('.json').write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n')
        print(f'已保存：{self.path}\n时长 {self.written / self.fps:.1f} 秒，'
              f'接收 {self.received} 帧，采样 {data["received_fps"]:.1f} FPS。', flush=True)
        return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['cap'])
    parser.add_argument('--board', default=os.environ.get('XUNJING_BOARD', 'root@192.168.126.49'))
    parser.add_argument('--fps', type=int, default=20)
    parser.add_argument('--width', type=int, default=1280)
    parser.add_argument('--seconds', type=float, default=0)
    args = parser.parse_args()
    if not 1 <= args.fps <= 30 or not 320 <= args.width <= 1920 or args.width % 2:
        parser.error('fps=1..30，width=320..1920 且必须为偶数')
    if args.seconds < 0 or args.board.startswith('-'):
        parser.error('无效的录制参数')
    runtime = Path(os.environ.get('XDG_RUNTIME_DIR', str(Path.home() / '.cache')))
    runtime.mkdir(parents=True, exist_ok=True)
    lock = (runtime / 'xunjing-hdmi-capture.lock').open('a')
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        print('已有录屏运行，请先在原终端按 Ctrl+C。', file=sys.stderr)
        return 1

    stopping = False

    def stop(*_):
        nonlocal stopping
        stopping = True

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)
    cv2.setNumThreads(2)
    source = Path(__file__).with_name('stream_hdmi.py').read_text()
    command = 'exec nice -n 10 python3 -u -c ' + shlex.quote(source)
    command += f' --fps {args.fps} --width {args.width} --seconds {args.seconds}'
    print(f'连接开发板 {args.board}，读取 HDMI 检测画面…', flush=True)
    proc = subprocess.Popen(['ssh', '-T', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5',
                             '-o', 'ServerAliveInterval=3', '-o', 'ServerAliveCountMax=2',
                             args.board, command], stdin=subprocess.DEVNULL,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
    selector = selectors.DefaultSelector()
    selector.register(proc.stdout, selectors.EVENT_READ, 'video')
    selector.register(proc.stderr, selectors.EVENT_READ, 'error')
    buffer = bytearray()
    errors = deque(maxlen=16)
    sink = VideoSink(args.fps)
    error = None
    last_data = time.monotonic()
    try:
        while not stopping and selector.get_map():
            for key, _ in selector.select(timeout=0.2):
                data = os.read(key.fileobj.fileno(), 256 * 1024)
                if not data:
                    selector.unregister(key.fileobj)
                    continue
                if key.data == 'error':
                    errors.append(data.decode('utf-8', errors='replace'))
                    continue
                last_data = time.monotonic()
                buffer.extend(data)
                while len(buffer) >= PACKET.size:
                    magic, size, timestamp, read_ms, encode_ms = PACKET.unpack_from(buffer)
                    if magic not in (b'XJCP', b'XJL4') or not 1 <= size <= 16 * 1024 * 1024:
                        raise RuntimeError('录屏数据格式错误')
                    if len(buffer) < PACKET.size + size:
                        break
                    jpeg = bytes(buffer[PACKET.size:PACKET.size + size])
                    del buffer[:PACKET.size + size]
                    sink.add(jpeg, timestamp, read_ms, encode_ms, magic)
            if time.monotonic() - last_data > 15:
                raise RuntimeError('超过 15 秒未收到画面，请检查主程序和网络')
        if not stopping:
            status = proc.wait(timeout=3)
            if status or not sink.received:
                raise RuntimeError(''.join(errors).strip() or '板端未返回画面')
            if buffer:
                raise RuntimeError('图像传输中断，最后一帧不完整')
        else:
            print('\n正在保存视频…', flush=True)
    except Exception as exc:
        error = str(exc)
        print(f'录屏结束：{error}', file=sys.stderr)
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        selector.close()
        proc.stdout.close()
        proc.stderr.close()
        sink.close(error, args.board)
    return 1 if error or not sink.received else 0


if __name__ == '__main__':
    raise SystemExit(main())
