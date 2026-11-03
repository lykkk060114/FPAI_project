#!/usr/bin/env python3
"""Generate a ZG330 Qwen summary from an explicitly selected observation JSON."""
import argparse
import datetime as dt
import fcntl
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid

from behavior_report import LABELS, atomic_json
from push_qwen_result import push_result

ROOT = Path(__file__).resolve().parents[1]


def load_statistics(path):
    report = json.loads(path.read_text())
    if not isinstance(report, dict) or not isinstance(report.get('days'), dict):
        raise ValueError('Expected an observation JSON containing days')
    for day, entry in report['days'].items():
        dt.date.fromisoformat(day)
        values = [entry['target_observed_seconds'], entry['unknown_behavior_seconds']]
        values += [entry['actions_seconds'][label] for label in LABELS]
        if any(isinstance(v, bool) or not isinstance(v, (float, int)) or
               not math.isfinite(v) or v < 0 for v in values):
            raise ValueError('Durations must be finite nonnegative numbers')
        if abs(sum(values[1:]) - values[0]) > 0.01:
            raise ValueError('Action and unknown durations do not sum to observed duration')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True, help='Observation JSON saved by the main program')
    args = parser.parse_args()
    try:
        source = args.config.expanduser().resolve(strict=True)
        report = load_statistics(source)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))
    results = ROOT / 'results'
    results.mkdir(exist_ok=True)
    # Use the same lock as the observation launcher, preventing NPU overlap.
    with (results / 'observation.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise SystemExit('Detection or Qwen is running. Run stop and wait before using qwen.')
        runner = ROOT.parent / 'qwen_behavior_summary/zg330/tools/generate_summary_zg330.py'
        if not runner.is_file():
            raise SystemExit('ZG330 Qwen package is not deployed: ' + str(runner))
        session = results / 'manual_qwen' / uuid.uuid4().hex
        session.mkdir(parents=True)
        atomic_json(session / 'statistics.json', report)
        metadata = {'source_json': str(source), 'phase': 'summarizing', 'backend': 'zg330'}
        atomic_json(session / 'session.json', metadata)
        env = os.environ.copy()
        paths = [p for p in env.get('LD_LIBRARY_PATH', '').split(':')
                 if p and Path(p).resolve() != (ROOT / 'lib').resolve()]
        if paths:
            env['LD_LIBRARY_PATH'] = ':'.join(paths)
        else:
            env.pop('LD_LIBRARY_PATH', None)
        started = time.monotonic()
        outputs = []
        rc = 0
        # The generator accepts one calendar day per invocation. Keep day
        # boundaries explicit for observations spanning midnight.
        days = sorted(report['days'])
        for day in days or [None]:
            target = session if len(days) <= 1 else session / day
            target.mkdir(exist_ok=True)
            selected = {} if day is None else {day: report['days'][day]}
            atomic_json(target / 'statistics.json', dict(report, days=selected))
            atomic_json(target / 'summary_input.json', {'scope': 'session_selected_target', 'days': selected})
            code = subprocess.call([sys.executable, str(runner), '--session', str(target)], env=env)
            result = json.loads((target / 'summary.json').read_text()) if (target / 'summary.json').exists() else {
                'status': 'generation_failed', 'error': 'No summary.json was produced', 'text': ''}
            if result['status'] in ('generation_failed', 'generation_timeout', 'generation_rejected') and code == 0:
                code = 1
            outputs.append({'date': day, **result})
            if result.get('text'):
                print('[QWEN SUMMARY] ' + result['text'], flush=True)
            if code:
                rc = code if code > 0 else 128 - code
                print('[QWEN ERROR] ' + result.get('error', result['status']), file=sys.stderr, flush=True)
                break
        wall = time.monotonic() - started
        if len(days) > 1:
            atomic_json(session / 'summary.json', {
                'status': 'generated_unreviewed' if rc == 0 else 'generation_failed',
                'backend': 'iCraft ZG330', 'days': outputs,
                'text': '\n'.join(x.get('text', '') for x in outputs),
                'timing': {'total_wall_seconds': wall}})
            (session / 'qwen_output.txt').write_text('\n'.join(x.get('text', '') for x in outputs))
        atomic_json(session / 'manual_timing.json', {'backend': 'zg330', 'wall_seconds': wall, 'exit_code': rc})
        metadata.update(phase='finished' if rc == 0 else 'summary_failed', wall_seconds=wall, exit_code=rc)
        atomic_json(session / 'session.json', metadata)
        print('[QWEN] %.3f s; results: %s' % (wall, session), flush=True)
        push_result(session)
        return rc


if __name__ == '__main__':
    raise SystemExit(main())
