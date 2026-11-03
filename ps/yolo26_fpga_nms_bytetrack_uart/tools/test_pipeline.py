#!/usr/bin/env python3
"""Timed board pipeline test using PTY UART, not real MCU verification."""
import argparse,csv,json,os,pty,select,signal,subprocess,time,hashlib,shutil
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--config',type=Path);ap.add_argument('--backend',choices=['cpu','zg330'],default='zg330');ap.add_argument('--seconds',type=int,default=60);ap.add_argument('--warmup-seconds',type=int,default=10);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
p=Path(__file__).resolve().parents[1];r=a.output.resolve();r.mkdir(parents=True,exist_ok=False)
for proc in Path('/proc').glob('[0-9]*/cmdline'):
 try:
  name=Path(proc.read_bytes().split(bytes([0]))[0].decode()).name
  if name.startswith('sdicamera+') or name=='classifier_benchmark':raise SystemExit('Inference already running; stop it first')
 except (OSError,UnicodeError):pass
config='sdicamera+yolo26+bytetrack+classify'+('_zg330' if a.backend=='zg330' else '')+'+range+hdmi_60.yaml'
config_path=a.config.resolve() if a.config else p/'configs'/config
master,slave=pty.openpty();s=config_path.read_text().replace('device: /dev/ttyUSB0','device: '+os.ttyname(slave));(r/'runtime.yaml').write_text(s)
env=os.environ.copy();env['LD_LIBRARY_PATH']=str(p/'lib')+':'+env.get('LD_LIBRARY_PATH','');env['XUNJING_STAGE_TIMING_CSV']=str(r/'stages.csv')
for key in ['XUNJING_REFERENCE_DIR','XUNJING_LINK_MAPPING_DIR','XUNJING_LINK_INPUT_REFERENCE_DIR']:env.pop(key,None)
before=set((p/'results').glob('*.csv'));start=time.monotonic();data=bytearray();forced=False;ready_at=None;measure_start=None;tail=b'';read_pos=0
if a.seconds<1 or a.warmup_seconds<0:raise SystemExit('Invalid duration')
with (r/'pipeline.log').open('wb') as log:
 proc=subprocess.Popen([str(p/'bin/sdicamera+yolo26+bytetrack+hdmi'),str(r/'runtime.yaml')],cwd=p,env=env,stdin=subprocess.PIPE,stdout=log,stderr=subprocess.STDOUT)
 try:
  while proc.poll() is None:
   now=time.monotonic()
   with (r/'pipeline.log').open('rb') as reader:
    reader.seek(read_pos);part=reader.read();read_pos=reader.tell()
   tail=(tail+part)[-65536:]
   if ready_at is None and b'All actors started' in tail:
    ready_at=now;measure_start=now+a.warmup_seconds;print('Actors ready; warming up then measuring',flush=True)
   if ready_at is None and now-start>120:break
   if measure_start is not None and now>=measure_start+a.seconds:break
   ready,_,_=select.select([master],[],[],.2)
   if ready:
    try:data.extend(os.read(master,65536))
    except OSError:pass
 finally:
  if proc.poll() is None:
   proc.send_signal(signal.SIGTERM)
   try:proc.wait(timeout=20)
   except subprocess.TimeoutExpired:forced=True;proc.kill();proc.wait()
  proc.stdin.close();os.close(master);os.close(slave)
(r/'uart.bin').write_bytes(data)
for f in set((p/'results').glob('*.csv'))-before:shutil.copy2(f,r/f.name)
valid=bad=0;pos=0
while pos+19<=len(data):
 if data[pos:pos+2]!=bytes([170,85]):pos+=1;continue
 b=data[pos:pos+19];crc=65535
 for v in b[:17]:
  crc^=v<<8
  for _ in range(8):crc=((crc<<1)^0x1021 if crc&32768 else crc<<1)&65535
 if crc==int.from_bytes(b[17:19],'little') and b[2:4]==bytes([2,1]) and b[6:8]==bytes([9,0]):valid+=1;pos+=19
 else:bad+=1;pos+=1
text=(r/'pipeline.log').read_text(errors='replace');report={'backend':a.backend,'config_source':str(config_path),'initialization_observed_s':None if ready_at is None else ready_at-start,'warmup_s':a.warmup_seconds,'measure_start_steady_ns':None if measure_start is None else int(measure_start*1e9),'measurement_requested_s':a.seconds,'elapsed_s':time.monotonic()-start,'exit_code':proc.returncode,'forced_kill':forced,'camera_ok':text.count('ps_frame_ok'),'camera_error':text.count('ps_frame_error'),'pty_crc_valid':valid,'pty_crc_invalid':bad,'real_mcu':False,'binary_sha256':hashlib.sha256((p/'bin/sdicamera+yolo26+bytetrack+hdmi').read_bytes()).hexdigest(),'steady_clock_used':True,'board_wall_clock_may_be_wrong':True}
if (r/'stages.csv').exists():
 rows=list(csv.DictReader((r/'stages.csv').open()));report['classifier_jobs']=sum(x['stage']=='classifier_total' and x['ok']=='1' for x in rows);report['failed_stage_rows']=sum(x['ok']=='0' for x in rows)
(r/'run.json').write_text(json.dumps(report,indent=2)+chr(10));print(json.dumps(report,indent=2))
raise SystemExit(0 if proc.returncode==0 and not forced and report['camera_ok']>0 and report['camera_error']==0 else 1)
