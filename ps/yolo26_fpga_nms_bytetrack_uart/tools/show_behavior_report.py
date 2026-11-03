#!/usr/bin/env python3
"""Show the latest observation state and its manual Qwen command."""
import json
from pathlib import Path
root=Path(__file__).resolve().parents[1]/'results'
pointer=root/'current_behavior_session.json'
if pointer.exists():
    session=Path(json.loads(pointer.read_text())['session'])
elif (root/'latest_behavior_report.json').exists():
    session=Path(json.loads((root/'latest_behavior_report.json').read_text())['statistics']).parent
else:
    raise SystemExit('No observation report yet')
print('Session:',session)
meta=session/'session.json'
if meta.exists():
    state=json.loads(meta.read_text())
    print('Phase:',state.get('phase','unknown'))
stats=session/'statistics.json'
if stats.exists():
    report=json.loads(stats.read_text())
    print('Calendar reliable:',report.get('calendar_reliable'))
    for day,entry in report.get('days',{}).items():
        print(day,'observed seconds:',round(entry['target_observed_seconds'],2))
        for action,value in entry['actions'].items():
            print(' ',action,round(value['seconds'],2),'s;',value['percent_of_observed'],'% of observed time')
summary=session/'summary.json'
if summary.exists():
    result=json.loads(summary.read_text())
    print('Qwen:',result.get('status'),'backend:',result.get('backend'))
    print(result.get('text',result.get('error','')))
    print('Timing:',result.get('timing',{}))
else:
    exported=state.get('result_json') if meta.exists() else None
    if exported:
        print('Saved JSON:',exported)
        print('Manual summary: qwen --config',exported)
    else:
        print('Statistics will be saved when detection exits; Qwen is manual.')
