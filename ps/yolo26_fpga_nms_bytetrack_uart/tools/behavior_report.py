#!/usr/bin/env python3
"""Post-exit observation accounting. No extrapolation and no medical diagnosis."""
import argparse,csv,datetime as dt,json,math,os,time
from pathlib import Path
LABELS=('drink','eat','rest','walk')
TZ=dt.timezone(dt.timedelta(hours=8))
def atomic_json(path,data):
 path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
 tmp=path.with_name(path.name+'.pending')
 with tmp.open('w') as f:
  json.dump(data,f,ensure_ascii=False,indent=2,allow_nan=False);f.flush();os.fsync(f.fileno())
 os.replace(str(tmp),str(path))
def bucket():
 return dict(pipeline_covered_seconds=0.,pipeline_gap_seconds=0.,target_observed_seconds=0.,no_target_seconds=0.,target_transition_seconds=0.,unknown_behavior_seconds=0.,actions_seconds={k:0. for k in LABELS},tracks={})
def summarize(csv_path,session_id,clock=None,max_gap=.25,confidence=.55):
 started=time.perf_counter();days={};previous={};first=None;last=None;rows=bad=0;max_step=0.
 with Path(csv_path).open(newline='') as f:
  for raw in csv.DictReader(f):
   try:
    steady=int(raw['steady_ns']);src=int(raw['source_id']);tid=int(raw['track_id']);bid=int(raw['behavior_id']);conf=float(raw['behavior_confidence']);wall=int(raw['wall_epoch_ns'])
    if not math.isfinite(conf) or steady<0:raise ValueError()
   except (KeyError,TypeError,ValueError):bad+=1;continue
   rows+=1;first=steady if first is None else min(first,steady);last=steady if last is None else max(last,steady)
   label=LABELS[bid] if 0<=bid<len(LABELS) and conf>=confidence else None
   if clock:
    epoch=(int(clock['utc_epoch_ns'])+steady-int(clock['steady_ns']))/1e9
   else:epoch=wall/1e9
   current=(steady,tid,label,epoch)
   prev=previous.get(src)
   if prev is None:previous[src]=current;continue
   delta=(steady-prev[0])/1e9
   if delta<=0:bad+=1;continue
   previous[src]=current
   max_step=max(max_step,delta)
   # Split intervals at local midnight. A recording gap never becomes an action.
   cursor=prev[3];end=cursor+delta
   while cursor<end-1e-8:
    date=dt.datetime.fromtimestamp(cursor,TZ);midnight=dt.datetime.combine(date.date()+dt.timedelta(days=1),dt.time(),TZ).timestamp();stop=min(end,midnight);seconds=stop-cursor;cursor=stop
    b=days.setdefault(date.date().isoformat(),bucket())
    if delta>max_gap:b['pipeline_gap_seconds']+=seconds;continue
    b['pipeline_covered_seconds']+=seconds
    if prev[1]<0 or tid<0:b['no_target_seconds']+=seconds;continue
    if prev[1]!=tid:b['target_transition_seconds']+=seconds;continue
    b['target_observed_seconds']+=seconds
    key='%s:source%d:track%d'%(session_id,src,tid)
    tr=b['tracks'].setdefault(key,dict(observed_seconds=0.,unknown_seconds=0.,actions_seconds={k:0. for k in LABELS}))
    tr['observed_seconds']+=seconds
    if prev[2] is not None and prev[2]==label:
     b['actions_seconds'][label]+=seconds;tr['actions_seconds'][label]+=seconds
    else:b['unknown_behavior_seconds']+=seconds;tr['unknown_seconds']+=seconds
 for b in days.values():decorate(b)
 return dict(schema_version=1,session_id=session_id,input_csv=str(Path(csv_path).resolve()),scope='selected_locked_target_only_not_all_visible_pandas',identity='ByteTrack IDs are session-local track fragments, not biological identity',time_zone='Asia/Shanghai',clock_source='host_utc_monotonic_anchor' if clock else 'board_rtc_unverified',calendar_reliable=bool(clock),confidence_min=confidence,max_accepted_frame_gap_seconds=max_gap,rows=rows,malformed_or_nonmonotonic_rows=bad,sampled_span_seconds=0. if first is None else (last-first)/1e9,max_frame_gap_seconds=max_step,days=days,health_status='not_assessable_from_behavior_only',timing={'statistics_wall_ms':(time.perf_counter()-started)*1000})
def decorate(b):
 observed=b['target_observed_seconds'];known=sum(b['actions_seconds'].values())
 b['classified_seconds']=known;b['actions']={k:dict(seconds=v,percent_of_observed=100*v/observed if observed else None,percent_of_24h=100*v/86400) for k,v in b['actions_seconds'].items()}
 b['observed_percent_of_24h']=100*observed/86400
 b['unobserved_seconds_of_24h']=max(0.,86400-observed)
 b['percentages_are_not_extrapolated']=True
def daily_aggregate(root):
 days={};sessions=[]
 for path in sorted(Path(root).glob('*/statistics.json')):
  report=json.loads(path.read_text());sessions.append(report['session_id'])
  # Unverified RTC cannot silently pollute calendar-day totals.
  if not report['calendar_reliable']:continue
  for day,entry in report['days'].items():
   b=days.setdefault(day,bucket())
   for field in ('pipeline_covered_seconds','pipeline_gap_seconds','target_observed_seconds','no_target_seconds','target_transition_seconds','unknown_behavior_seconds'):b[field]+=entry[field]
   for label in LABELS:b['actions_seconds'][label]+=entry['actions_seconds'][label]
   b['tracks'].update(entry['tracks'])
 for b in days.values():decorate(b)
 return dict(schema_version=1,days=days,sessions=sessions,scope='single_nonconcurrent_camera_selected_target',health_status='not_assessable_from_behavior_only')
def prompt_for(report):
 labels={'drink':'喝水','eat':'进食','rest':'休息','walk':'行走'}
 lines=[]
 for day,b in report['days'].items():
  actions='，'.join('%s %.1f秒'%(labels[k],v) for k,v in b['actions_seconds'].items())
  lines.append('%s：目标实际观察%.1f秒；%s；未知动作%.1f秒；实际观察占全天%.4f%%。'%(day,b['target_observed_seconds'],actions,b['unknown_behavior_seconds'],b['observed_percent_of_24h']))
 return ('请用中文写熊猫行为观察总结，100字以内。只根据下列记录。记录来自单个选定目标，不能代表所有熊猫。'+
 '未观察时间不能当作休息，不能把短时观察推算成全天。不得推断健康、患病或情绪。'+
 '结尾必须写：仅凭行为记录不能判断健康，需饲养员和兽医结合长期记录评估。'+
 '日历可靠：'+str(report['calendar_reliable'])+'。记录：'+'\n'.join(lines))
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--csv',required=True,type=Path);p.add_argument('--session',required=True,type=Path);a=p.parse_args()
 meta=json.loads((a.session/'session.json').read_text());report=summarize(a.csv,meta['session_id'],meta.get('clock_anchor'))
 atomic_json(a.session/'statistics.json',report)
 (a.session/'prompt.txt').write_text(prompt_for(report))
 atomic_json(a.session.parent/'daily_statistics.json',daily_aggregate(a.session.parent))
 print('Behavior statistics:',a.session/'statistics.json',flush=True)
if __name__=='__main__':main()
