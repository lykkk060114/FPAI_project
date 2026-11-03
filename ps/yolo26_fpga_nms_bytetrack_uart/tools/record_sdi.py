#!/usr/bin/env python3
"""Board-side SDI recording with acquisition timestamps; default raw RGB565LE at 5 FPS."""
import argparse,csv,datetime,fcntl,hashlib,json,os,re,shutil,signal,subprocess,time
from pathlib import Path

def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--name',required=True,help='Batch name, e.g. panda_far_01 or background_desk_01')
 ap.add_argument('--kind',choices=['positive','negative','mixed'],default='mixed',help='Shooting intent only, not automatic labels')
 ap.add_argument('--seconds',type=float,default=60,help='Recording time after init; 0 until Ctrl+C')
 ap.add_argument('--fps',type=float,default=5,help='Requested saved FPS; does not change sensor FPS')
 ap.add_argument('--codec',choices=['raw','mjpeg','ffv1'],default='raw',help='Raw RGB565 (fast, large files), MJPEG or FFV1')
 ap.add_argument('--root',type=Path,default=Path('/root/workspace/camera_dataset/videos'))
 a=ap.parse_args()
 if not re.fullmatch(r'[A-Za-z0-9_-]{1,64}',a.name):ap.error('Use 1..64 ASCII letters/digits/_/- for name')
 if not 1<=a.fps<=30 or not 0<=a.seconds<=86400:ap.error('fps: 1..30; seconds: 0..86400')
 p=Path(__file__).resolve().parents[1]
 lock=open('/tmp/xunjing_sdi_record.lock','w')
 try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
 except BlockingIOError:raise SystemExit('Another recording is running')
 for f in Path('/proc').glob('[0-9]*/cmdline'):
  try:
   name=Path(f.read_bytes().split(bytes([0]))[0].decode()).name
   if name.startswith('sdicamera+') or name in ['sdi_capture','classifier_zg330','classifier_benchmark']:
    raise SystemExit('Camera/NPU process already running: '+name+'; stop it first')
  except (OSError,UnicodeError):pass
 for executable in ['ffmpeg','ffprobe']:
  if not shutil.which(executable):raise SystemExit('Missing dependency: '+executable)
 a.root.mkdir(parents=True,exist_ok=True)
 if shutil.disk_usage(a.root).free<1024**3:raise SystemExit('Less than 1 GiB free; clear disk before recording')
 # Board RTC may be wrong. Host script writes its trusted start time separately.
 stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
 r=a.root/(stamp+'_'+a.name+'_'+str(os.getpid()));r.mkdir()
 env=os.environ.copy();env['LD_LIBRARY_PATH']=str(p/'lib')+':'+env.get('LD_LIBRARY_PATH','')
 cmd=['ffmpeg','-hide_banner','-loglevel','warning','-nostdin','-f','rawvideo','-pixel_format','rgb565le','-video_size','1920x1080','-framerate',str(a.fps),'-i','pipe:0','-an','-c:v','ffv1','-level','3','-threads','2','-pix_fmt','bgr0',str(r/'video.mkv')]
 if a.codec=='mjpeg':
  cmd=cmd[:cmd.index('-c:v')]+['-c:v','mjpeg','-q:v','2','-threads','2','-pix_fmt','yuvj422p',str(r/'video.mkv')]
 video=r/('video.nut' if a.codec=='raw' else 'video.mkv')
 if a.codec=='raw':cmd=cmd[:cmd.index('-c:v')]+['-c:v','rawvideo','-pix_fmt','rgb565le','-f','nut',str(video)]
 start=time.monotonic();interrupted=False;capture=None;encoder=None
 print('Recording directory: '+str(r),flush=True)
 print('1920x1080, no detection/overlay, codec='+a.codec+', requested FPS='+str(a.fps)+'; Ctrl+C stops recording.',flush=True)
 stop_requested=False
 def request_stop(signum,frame):
  nonlocal stop_requested,interrupted
  if not stop_requested:print('Stopping; finishing video and checking frames. Please wait for Saved:',flush=True)
  stop_requested=True;interrupted=True
 signal.signal(signal.SIGINT,request_stop)
 # Parent owns Ctrl+C through verification; repeated presses cannot interrupt cleanup.
 with (r/'capture.log').open('wb') as caplog,(r/'encoder.log').open('wb') as enclog:
  encoder=subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=subprocess.DEVNULL,stderr=enclog,start_new_session=True)
  try:
   capture=subprocess.Popen([str(p/'bin/sdi_capture'),str(a.fps),str(a.seconds),str(r/'timestamps.csv')],cwd=p,env=env,stdout=encoder.stdin,stderr=caplog,start_new_session=True)
   encoder.stdin.close()
   try:
    while capture.poll() is None:
     if stop_requested:
      capture.send_signal(signal.SIGTERM);break
     if shutil.disk_usage(r).free<512*1024**2:
      print('Stopping: disk free space below 512 MiB',flush=True);capture.send_signal(signal.SIGTERM);break
     time.sleep(.25)
   except KeyboardInterrupt:
    interrupted=True;capture.send_signal(signal.SIGTERM)
   print('Finalizing video and checking frames; HDMI preview is not enabled.',flush=True)
   try:capture.wait(timeout=15)
   except subprocess.TimeoutExpired:capture.kill();capture.wait()
  finally:
   if encoder.stdin and not encoder.stdin.closed:encoder.stdin.close()
   if capture is not None and capture.poll() is None:capture.kill();capture.wait()
   try:encoder.wait(timeout=30)
   except subprocess.TimeoutExpired:encoder.kill();encoder.wait()
 rows=list(csv.DictReader((r/'timestamps.csv').open())) if (r/'timestamps.csv').exists() else []
 good=[x for x in rows if x.get('ok')=='1'];duration=0
 if len(good)>1:duration=(int(good[-1]['done_steady_ns'])-int(good[0]['done_steady_ns']))/1e9
 probe=subprocess.run(['ffprobe','-v','error','-count_frames','-select_streams','v:0','-show_entries','stream=width,height,codec_name,nb_read_frames','-of','json',str(video)],capture_output=True,text=True)
 stream={}
 if probe.returncode==0:
  streams=json.loads(probe.stdout).get('streams',[]);stream=streams[0] if streams else {}
 frames=int(stream.get('nb_read_frames',0))
 verified=bool(good) and frames==len(good) and capture is not None and capture.returncode==0 and encoder.returncode==0 and stream.get('width')==1920 and stream.get('height')==1080
 report={'shooting_intent':a.kind,'labels_verified':False,'name':a.name,'board_utc_time_untrusted':stamp,'format':'FFV1 BGR0 converted from RGB565LE; no overlays','width':1920,'height':1080,'requested_fps':a.fps,'sensor_fps_configured':False,'requested_seconds':a.seconds,'process_elapsed_s':time.monotonic()-start,'successful_frames':len(good),'capture_errors':len(rows)-len(good),'actual_capture_fps':(len(good)-1)/duration if duration>0 else None,'encoded_frames':frames,'video_playback_duration_note':'CFR nominal fps; actual acquisition intervals are in timestamps.csv','capture_exit':None if capture is None else capture.returncode,'encoder_exit':encoder.returncode,'interrupted_by_user':interrupted,'verified':verified,'binary_sha256':hashlib.sha256((p/'bin/sdi_capture').read_bytes()).hexdigest()}
 report['video_file']=video.name
 report['codec']=a.codec
 report['format']=('Uncompressed RGB565LE' if a.codec=='raw' else 'MJPEG quality 2 YUV422 (lossy)' if a.codec=='mjpeg' else 'FFV1 lossless BGR0')+' converted from RGB565LE; no overlays'
 (r/'recording.json').write_text(json.dumps(report,indent=2)+chr(10))
 print(json.dumps(report,indent=2),flush=True)
 print('Saved: '+str(video),flush=True)
 return 0 if verified else 1
if __name__=='__main__':raise SystemExit(main())
