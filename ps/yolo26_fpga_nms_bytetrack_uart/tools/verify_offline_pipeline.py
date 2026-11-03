#!/usr/bin/env python3
"""Run the production C++ pipeline with a virtual MCU on a POSIX pseudo-TTY."""
import argparse
import binascii
import json
import math
import os
from pathlib import Path
import pty
import select
import struct
import subprocess
import threading


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--detector', type=Path, required=True)
    parser.add_argument('--classifier', type=Path, required=True)
    parser.add_argument('--video', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--frames', type=int, default=60)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    master, slave = pty.openpty()
    received = bytearray()
    stopped = threading.Event()

    def read():
        while not stopped.is_set():
            if select.select([master], [], [], .1)[0]:
                received.extend(os.read(master, 4096))

    reader = threading.Thread(target=read)
    reader.start()
    try:
        command = [str(args.binary.resolve()), str(args.detector.resolve()),
                   str(args.classifier.resolve()), str(args.video.resolve()),
                   str((args.output/'pipeline.json').resolve()), os.ttyname(slave), str(args.frames)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=180)
        # Drain bytes still queued when the process exited.
        stopped.set(); reader.join()
        while select.select([master], [], [], .05)[0]: received.extend(os.read(master, 4096))
    finally:
        stopped.set(); reader.join(); os.close(master); os.close(slave)
    (args.output/'process.log').write_text(result.stdout + result.stderr)
    if result.returncode: raise RuntimeError(result.stdout + result.stderr)
    assert received and len(received) % 19 == 0, 'UART stream truncated or empty'
    messages = []
    for start in range(0, len(received), 19):
        frame = bytes(received[start:start+19])
        assert frame[:4] == bytes.fromhex('aa550201')
        sequence, length = struct.unpack_from('<HH', frame, 4)
        assert sequence == len(messages) % 65536 and length == 9
        assert binascii.crc_hqx(frame[:17], 0xffff) == struct.unpack_from('<H', frame, 17)[0]
        assert frame[16] <= 4
        distance, x, y = struct.unpack_from('<fHH', frame, 8)
        assert math.isfinite(distance) and distance >= 0 and 0 <= x < 1920 and 0 <= y < 1080
        messages.append(dict(sequence=sequence, distance_m=distance, center=[x,y]))
    pipeline = json.loads((args.output/'pipeline.json').read_text())
    assert pipeline['summary']['frames'] == args.frames
    assert pipeline['summary']['fresh_classifications'] >= 2
    report = dict(scope='Host ONNX models and production C++ modules; PTY replaces physical MCU; no board/NPU test',
                  summary=pipeline['summary'], uart_frames_received=len(messages),
                  all_crc_valid=True, uart_messages=messages)
    (args.output/'verification.json').write_text(json.dumps(report, indent=2)+'\n')
    (args.output/'uart_stream.bin').write_bytes(received)
    print(json.dumps({k:v for k,v in report.items() if k!='uart_messages'}))


if __name__ == '__main__': main()
