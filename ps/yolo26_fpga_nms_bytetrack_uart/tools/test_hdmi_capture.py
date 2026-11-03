#!/usr/bin/python3
"""Offline protocol, playback duration, and interrupt-finalization checks."""
import json
import os
from pathlib import Path
import selectors
import signal
import struct
import subprocess
import sys
import tempfile
import unittest

import cv2
import numpy as np

import capture_hdmi
import stream_hdmi


class CaptureTest(unittest.TestCase):
    def test_timestamp_gaps_do_not_speed_up_playback(self):
        with tempfile.TemporaryDirectory() as directory:
            sink = capture_hdmi.VideoSink(20, Path(directory))
            image = np.full((180, 320, 3), (0, 0, 255), np.uint8)
            ok, jpeg = cv2.imencode('.jpg', image)
            self.assertTrue(ok)
            for timestamp in (10.0, 10.05, 10.2):
                sink.add(jpeg.tobytes(), timestamp, 1, 2)
            report = sink.close()
            self.assertEqual(report['received_frames'], 3)
            self.assertEqual(report['encoded_frames'], 5)
            cap = cv2.VideoCapture(str(sink.path))
            self.assertEqual(cap.get(cv2.CAP_PROP_FRAME_COUNT), 5)
            self.assertAlmostEqual(cap.get(cv2.CAP_PROP_FPS), 20)
            ok, decoded = cap.read()
            cap.release()
            self.assertTrue(ok)
            self.assertGreater(decoded[:, :, 2].mean(), 240)
            # A second recording in the same minute must preserve the first.
            other = capture_hdmi.VideoSink(20, Path(directory))
            other.add(jpeg.tobytes(), 20, 1, 2)
            other.close()
            self.assertNotEqual(sink.path, other.path)
            self.assertTrue(other.path.stem.endswith('_02'))

    def test_readonly_framebuffer_address_and_bounds(self):
        reader = stream_hdmi.HdmiReader.__new__(stream_hdmi.HdmiReader)
        reader.base = 0x30000000
        reader.size = 1920 * 1080 * 2
        reader.reg = bytearray(4096)
        reader.reg_offset = 0x54
        struct.pack_into('<I', reader.reg, 0x54, reader.base >> 3)
        reader.dma = bytes([0, 248]) * (1920 * 1080)
        frame = reader.read()
        self.assertEqual(frame.shape, (1080, 1920, 2))
        self.assertEqual(tuple(frame[0, 0]), (0, 248))
        struct.pack_into('<I', reader.reg, 0x54, (reader.base - 8) >> 3)
        with self.assertRaises(RuntimeError):
            reader.read()

    def test_ctrl_c_and_broken_stream_finalize_mp4(self):
        tools = Path(__file__).resolve().parent
        for broken in (False, True):
            with self.subTest(broken=broken), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                fake = root / 'ssh'
                fake.write_text('#!/usr/bin/python3\n'
                    'import sys, time, os\n'
                    f'sys.path.insert(0, {str(tools)!r})\n'
                    'import numpy as np\nfrom stream_hdmi import stream\n'
                    'class Source:\n'
                    ' def read(self): return np.full((1080,1920,2), (0,248), np.uint8)\n'
                    f'stream(Source(),sys.stdout.buffer,20,1280,85,{0.35 if broken else 30})\n'
                    + ('sys.stdout.buffer.write(b"XJCPbad");sys.stdout.buffer.flush();sys.exit(1)\n'
                       if broken else ''))
                fake.chmod(0o755)
                wrapper = ('import sys; from pathlib import Path; '
                           f'sys.path.insert(0,{str(tools)!r}); import capture_hdmi as c; '
                           'original=c.VideoSink; '
                           f'c.VideoSink=lambda fps: original(fps,Path({str(root)!r})); '
                           'raise SystemExit(c.main())')
                env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ['PATH'])
                proc = subprocess.Popen([sys.executable, '-u', '-c', wrapper, 'cap'],
                                        env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        text=True)
                output = ''
                try:
                    if not broken:
                        selector = selectors.DefaultSelector()
                        selector.register(proc.stdout, selectors.EVENT_READ)
                        # First completed encoded frame is the readiness signal.
                        while '正在录制' not in output:
                            self.assertTrue(selector.select(8), 'No first frame')
                            line = proc.stdout.readline()
                            self.assertTrue(line, output)
                            output += line
                        selector.close()
                        proc.send_signal(signal.SIGINT)
                    rest, _ = proc.communicate(timeout=10)
                    output += rest
                finally:
                    if proc.poll() is None:
                        proc.kill()
                        proc.wait()
                self.assertEqual(proc.returncode, 1 if broken else 0, output)
                videos = list(root.glob('*.mp4'))
                self.assertEqual(len(videos), 1, output)
                cap = cv2.VideoCapture(str(videos[0]))
                ok, frame = cap.read()
                self.assertTrue(ok, output)
                self.assertEqual(frame.shape[:2], (720, 1280))
                cap.release()
                stats = json.loads(videos[0].with_suffix('.json').read_text())
                self.assertGreater(stats['received_frames'], 0)
                self.assertEqual(bool(stats['error']), broken)


if __name__ == '__main__':
    unittest.main(verbosity=2)
