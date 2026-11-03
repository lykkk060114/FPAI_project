#!/usr/bin/env python3
"""Send saved observation artifacts to the restricted host SSH receiver."""
import base64
import json
import os
from pathlib import Path
import subprocess
import time

from behavior_report import atomic_json


def push_result(session):
    session = Path(session)
    target = os.environ.get('XUNJING_QWEN_PUSH_TARGET')
    if not target:
        return
    names = ('summary.json', 'statistics.json', 'session.json',
             'summary_input.json', 'qwen_output.txt', 'qwen_stage_timing.csv',
             'summary_retry_timing.json', 'manual_timing.json')
    payload = {'session_id': session.name, 'files': {
        name: base64.b64encode((session / name).read_bytes()).decode('ascii')
        for name in names if (session / name).is_file()
    }}
    command = ['ssh', '-T', '-i', '/root/.ssh/xunjing_qwen_push',
               '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8',
               '-o', 'StrictHostKeyChecking=yes',
               '-o', 'UserKnownHostsFile=/root/.ssh/known_hosts_xunjing', target]
    started = time.monotonic()
    try:
        result = subprocess.run(command, input=json.dumps(payload).encode(),
                                capture_output=True, timeout=30)
        status = {'ok': result.returncode == 0, 'exit_code': result.returncode,
                  'response': result.stdout.decode(errors='replace'),
                  'error': result.stderr.decode(errors='replace')}
    except (OSError, subprocess.TimeoutExpired) as error:
        status = {'ok': False, 'error': str(error)}
    status.update(target=target, wall_seconds=time.monotonic() - started)
    atomic_json(session / 'host_transfer.json', status)
    print('[QWEN TRANSFER] ' + ('saved on host' if status['ok'] else
          'failed; board files retained: ' + status.get('error', '')), flush=True)


if __name__ == '__main__':
    import sys
    push_result(sys.argv[1])
