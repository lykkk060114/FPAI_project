#!/usr/bin/env python3
"""Run inference and save statistics on exit; Qwen is started manually."""
import datetime as dt,fcntl,hashlib,json,os,select,signal,subprocess,sys,time,uuid
from pathlib import Path
from behavior_report import atomic_json,summarize
from saved_observation import publish_statistics
ROOT=Path(__file__).resolve().parents[1]
def main():
 os.chdir(str(ROOT));results=ROOT/'results';results.mkdir(exist_ok=True)
 lock=(results/'observation.lock').open('a')
 try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
 except BlockingIOError:raise SystemExit('Detection or manual Qwen generation is already running')
 sid=uuid.uuid4().hex;session=results/'behavior_sessions'/sid;session.mkdir(parents=True)
 atomic_json(results/'current_behavior_session.json',dict(session_id=sid,session=str(session),metadata=str(session/'session.json')))
 boot=Path('/proc/sys/kernel/random/boot_id').read_text().strip();clock=None
 try:
  anchor=json.loads((ROOT/'results/observation_clock.json').read_text())
  if anchor['boot_id']==boot:clock=anchor
 except (OSError,ValueError,KeyError):pass
 config=Path(sys.argv[1]).resolve();meta=dict(session_id=sid,boot_id=boot,clock_anchor=clock,board_rtc_utc=dt.datetime.now(dt.timezone.utc).isoformat(),config=str(config),config_sha256=hashlib.sha256(config.read_bytes()).hexdigest(),binary_sha256=hashlib.sha256((ROOT/'bin/sdicamera+yolo26+bytetrack+hdmi').read_bytes()).hexdigest(),phase='detecting')
 atomic_json(session/'session.json',meta);before=set(results.glob('cpu_timing_*.csv'));child=None;requested=False
 def stop(signum,frame):
  nonlocal requested
  requested=True
  if child is not None and child.poll() is None:
   try:os.killpg(child.pid,signal.SIGTERM)
   except ProcessLookupError:pass
 signal.signal(signal.SIGINT,stop);signal.signal(signal.SIGTERM,stop)
 env=os.environ.copy();env['LD_LIBRARY_PATH']=str(ROOT/'lib')+':'+env.get('LD_LIBRARY_PATH','')
 started=time.monotonic();child=subprocess.Popen([str(ROOT/'bin/sdicamera+yolo26+bytetrack+hdmi'),str(config)],stdin=subprocess.PIPE,env=env,start_new_session=True)
 if requested:child.terminate()
 while child.poll() is None:
  if sys.stdin.isatty():
   readable,_,_=select.select([sys.stdin],[],[],.2)
   if readable:
    line=sys.stdin.readline()
    if line and child.stdin:
     try:child.stdin.write(line.encode());child.stdin.flush()
     except BrokenPipeError:pass
  else:time.sleep(.2)
 rc=child.wait();child.stdin.close();child=None
 meta.update(phase='statistics',inference_exit_code=rc,inference_wall_seconds=time.monotonic()-started,inference_exit_steady_ns=time.monotonic_ns());atomic_json(session/'session.json',meta)
 candidates=set(results.glob('cpu_timing_*.csv'))-before
 if len(candidates)>1:
  meta.update(phase='failed',error='Expected exactly one new timing CSV, found '+str(len(candidates)));atomic_json(session/'session.json',meta);return 1
 if candidates:
  csv_path=candidates.pop()
 else:
  # A stop during initialization or before the first frame has no timing CSV.
  # Save an explicitly empty record rather than losing this observation exit.
  csv_path=session/'empty_timing.csv'
  csv_path.write_text('steady_ns,source_id,track_id,behavior_id,behavior_confidence,wall_epoch_ns\n')
  meta['statistics_status']='no_frame_samples'
 report=summarize(csv_path,sid,clock)
 report['statistics_status']='recorded' if report['rows'] else 'no_frame_samples'
 atomic_json(session/'statistics.json',report)
 meta['summary_scope']='session_selected_target'
 atomic_json(session/'summary_input.json',dict(scope=meta['summary_scope'],days=report['days']))
 print('[REPORT] Main program exited. Statistics: '+str(session/'statistics.json'),flush=True)
 normal=rc in (0,-signal.SIGTERM,-signal.SIGINT,130,143)
 meta.update(phase='finished' if normal else 'inference_failed',summary_status='manual_only')
 saved=publish_statistics(report,meta)
 meta.update(result_json=str(saved),exit_finalize_wall_ms=(time.monotonic_ns()-meta['inference_exit_steady_ns'])/1e6)
 atomic_json(session/'session.json',meta)
 atomic_json(results/'latest_behavior_report.json',dict(session_id=sid,statistics=str(session/'statistics.json'),result_json=str(saved)))
 print('[REPORT] Saved: '+str(saved),flush=True)
 print('[REPORT] Exit finalization: %.3f ms'%meta['exit_finalize_wall_ms'],flush=True)
 print('[REPORT] Qwen is manual: qwen --config '+str(saved),flush=True)
 return 0 if normal else (rc if rc>=0 else 128-rc)
if __name__=='__main__':raise SystemExit(main())
