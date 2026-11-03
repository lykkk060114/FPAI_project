#!/usr/bin/env python3
"""Publish self-contained observation JSON and retain the latest five exports."""
import datetime as dt
import os
import time
from pathlib import Path

from behavior_report import atomic_json


def publish_statistics(report, metadata, directory=None):
    directory = Path(directory or os.environ.get('XUNJING_RESULT_JSON_DIR', '/root/result_json'))
    directory.mkdir(parents=True, exist_ok=True)
    anchor = metadata.get('clock_anchor')
    if anchor:
        epoch_ns = anchor['utc_epoch_ns'] + time.monotonic_ns() - anchor['steady_ns']
        now = dt.datetime.fromtimestamp(epoch_ns / 1e9, dt.timezone.utc)
    else:
        now = dt.datetime.now(dt.timezone.utc)
    stamp = now.strftime('%Y%m%dT%H%M%S%fZ')
    path = directory / ('observation_%s_%s.json' % (stamp, report['session_id']))
    payload = dict(report)
    payload['saved_at_utc'] = now.isoformat()
    payload['observation'] = dict(metadata)
    # Each file describes this observation, independently of older sessions.
    payload['summary_input'] = {'scope': 'session_selected_target', 'days': report['days']}
    atomic_json(path, payload)
    # Only manage exports from this writer; other user files are not pruned.
    older = sorted((p for p in directory.glob('observation_*.json')
                    if p != path and p.is_file() and not p.is_symlink()),
                   key=lambda p: (p.stat().st_mtime_ns, p.name), reverse=True)
    for old in older[4:]:
        old.unlink()
    return path
