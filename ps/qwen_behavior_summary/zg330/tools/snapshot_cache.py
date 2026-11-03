"""Versioned iCraft session cache; caches graph setup, never generated answers."""
import hashlib,json,os,platform,re,shutil,subprocess,time,uuid
from pathlib import Path

def digest(path):
 h=hashlib.sha256()
 with path.open('rb') as stream:
  for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
 return h.hexdigest()

def stamp(path):
 s=path.stat()
 return [s.st_dev,s.st_ino,s.st_size,s.st_mtime_ns,s.st_ctime_ns]

def describe(path,previous=None):
 path=path.resolve();current=stamp(path)
 # Unchanged local files need no repeated multi-GB hash pass. ctime detects
 # replacement/modification even if a deployment preserves the old mtime.
 sha=previous['sha256'] if previous and previous.get('stamp')==current else digest(path)
 if stamp(path)!=current:raise RuntimeError('cache input changed during verification: '+str(path))
 return {'stamp':current,'sha256':sha}

class SnapshotCache:
 def __init__(self,root,executable,mode='auto'):
  self.root=Path(root).resolve();self.executable=Path(executable).resolve();self.base=self.root/'snapshots'
  self.mode=mode;self.directory=None;self.sources={};self.previous={}
  self.info={'requested_mode':mode,'action':'off','validation_seconds':0.0}

 def prepare(self):
  if self.mode=='off':return []
  started=time.perf_counter();self.base.mkdir(parents=True,exist_ok=True)
  try:self.previous=json.loads((self.base/'active.json').read_text())
  except (OSError,ValueError):self.previous={}
  names=['decoder_%02d_c512'%i for i in range(24)]+['head']
  files=[self.executable]+[self.root/'runtime'/(n+'_ZG'+suffix) for n in names for suffix in ('.json','.raw')]
  files += [self.root/'runtime'/n for n in ('config.json','embedding.bf16','rope.f32')]
  libraries=subprocess.check_output(['ldd',str(self.executable)],text=True)
  files += [Path(p) for p in re.findall(r'libicraft_\S+\s+=>\s+(/\S+)',libraries)]
  if len([p for p in files if p.name.startswith('libicraft_')])<5:raise RuntimeError('cannot identify loaded iCraft libraries')
  files += [p for p in (Path('/mnt/fpai_boot/BOOT.bin'),Path('/mnt/fmql_boot_validation/BOOT.bin'),Path('/mnt/fmql_boot_inspect/BOOT.bin')) if p.is_file()]
  if not isinstance(self.previous,dict):self.previous={}
  for field in ('sources','snapshots'):
   records=self.previous.get(field,{})
   if not isinstance(records,dict) or any(not isinstance(r,dict) or not re.fullmatch(r'[0-9a-f]{64}',str(r.get('sha256',''))) or not isinstance(r.get('stamp'),list) or len(r['stamp'])!=5 for r in records.values()):
    self.previous={};break
  old=self.previous.get('sources',{})
  self.sources={str(p.resolve()):describe(p,old.get(str(p.resolve()))) for p in files}
  self.identity={'format':1,'machine':platform.machine(),'kernel':platform.release(),'context':512,'prefill_block':1,
                 'backend':'zg330','sdk_options':'disableEtmOptimize,disableMergeHardOp',
                 'ld_library_path':os.environ.get('LD_LIBRARY_PATH',''),'ld_preload':os.environ.get('LD_PRELOAD','')}
  valid=self.mode!='rebuild' and self.previous.get('identity')==self.identity and {p:v['sha256'] for p,v in self.sources.items()}=={p:v['sha256'] for p,v in old.items()}
  generation=self.previous.get('generation','')
  valid=valid and bool(re.fullmatch(r'g-[0-9a-f]{32}',generation))
  if valid:
   self.directory=self.base/generation
   try:
    actual={name:describe(self.directory/name,record) for name,record in self.previous.get('snapshots',{}).items() if name in self.expected()}
    valid=set(actual)==set(self.expected()) and all(actual[n]['sha256']==self.previous['snapshots'][n]['sha256'] for n in actual)
   except (OSError,ValueError):valid=False
  if valid:
   self.info.update(action='read',directory=str(self.directory),validation_seconds=time.perf_counter()-started)
   return ['--snapshot-mode','read','--snapshot-dir',str(self.directory)]
  required=sum(p.stat().st_size for p in files if p.suffix=='.raw')*1.1+64*1024*1024
  if shutil.disk_usage(self.base).free<required:
   self.info.update(action='off',reason='insufficient_disk_for_snapshot_build',validation_seconds=time.perf_counter()-started)
   return []
  self.directory=self.base/('g-'+uuid.uuid4().hex);self.directory.mkdir()
  self.info.update(action='write',directory=str(self.directory),validation_seconds=time.perf_counter()-started)
  return ['--snapshot-mode','write','--snapshot-dir',str(self.directory)]

 @staticmethod
 def expected():return ['decoder_%02d_c512.snapshot'%i for i in range(24)]+['head.snapshot']

 def finish(self,success):
  if self.info['action']!='write':return
  if not success:
   shutil.rmtree(self.directory);return
  started=time.perf_counter()
  if any(stamp(Path(p))!=record['stamp'] for p,record in self.sources.items()):raise RuntimeError('model/runtime changed during snapshot creation')
  records={name:describe(self.directory/name) for name in self.expected()}
  manifest={'identity':self.identity,'sources':self.sources,'generation':self.directory.name,'snapshots':records}
  pending=self.base/'active.pending';pending.write_text(json.dumps(manifest,indent=2));pending.replace(self.base/'active.json')
  self.info.update(publication_seconds=time.perf_counter()-started,cache_bytes=sum(r['stamp'][2] for r in records.values()))
  # Keep the last published generation for rollback; remove only our own older
  # generated caches after successfully publishing a complete replacement.
  keep={self.directory.name,self.previous.get('generation','')}
  for p in self.base.iterdir():
   if re.fullmatch(r'g-[0-9a-f]{32}',p.name) and p.name not in keep and p.is_dir():shutil.rmtree(p)

 def invalidate(self):
  active=self.base/'active.json'
  if active.exists():active.replace(self.base/'rejected.json')
