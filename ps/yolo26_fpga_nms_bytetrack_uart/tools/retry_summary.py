#!/usr/bin/env python3
"""Retry the latest saved observation summary without starting detection."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import subprocess
import sys
import time

from behavior_report import atomic_json

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path)
    args = parser.parse_args()
    with (ROOT / 'results/observation.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise SystemExit('Detection or Qwen is running; wait for it to finish')
        session = args.session
        if session is None:
            latest = json.loads((ROOT / 'results/latest_behavior_report.json').read_text())
            session = Path(latest['statistics']).parent
        session = session.resolve()
        if not (session / 'statistics.json').is_file():
            raise SystemExit('No saved statistics: ' + str(session))
        previous = session / 'summary.json'
        if previous.exists():
            previous.rename(session / ('summary_before_retry_%d.json' % time.time_ns()))
        env = dict(os.environ, XUNJING_QWEN_BACKEND='zg330')
        paths = [p for p in env.get('LD_LIBRARY_PATH', '').split(':')
                 if p and Path(p).resolve() != (ROOT / 'lib').resolve()]
        if paths:
            env['LD_LIBRARY_PATH'] = ':'.join(paths)
        else:
            env.pop('LD_LIBRARY_PATH', None)
        started = time.monotonic()
        rc = subprocess.call([
            sys.executable,
            str(ROOT.parent / 'qwen_behavior_summary/scripts/generate_summary.py'),
            '--session', str(session),
        ], env=env)
        atomic_json(session / 'summary_retry_timing.json', {
            'exit_code': rc, 'wall_seconds': time.monotonic() - started,
            'backend': 'zg330',
        })
        from push_qwen_result import push_result
        push_result(session)
        return rc


if __name__ == '__main__':
    raise SystemExit(main())
