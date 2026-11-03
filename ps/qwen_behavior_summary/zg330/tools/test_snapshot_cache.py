#!/usr/bin/env python3
"""Cache invalidation/publication checks with tiny files, no hardware claim."""
import json,tempfile
from pathlib import Path
from unittest.mock import patch
from snapshot_cache import SnapshotCache

def main():
 with tempfile.TemporaryDirectory() as temp:
  root=Path(temp);(root/'runtime').mkdir();exe=root/'runtime.bin';exe.write_bytes(b'executable-v1')
  for name in ['decoder_%02d_c512'%i for i in range(24)]+['head']:
   for suffix in ('.json','.raw'):(root/'runtime'/(name+'_ZG'+suffix)).write_bytes(b'weights-v1')
  for name in ('config.json','embedding.bf16','rope.f32'):(root/'runtime'/name).write_bytes(b'config-v1')
  libs=[]
  for name in ('xrt','xir','utils','hostbackend','zg330backend'):
   lib=root/('libicraft_'+name+'.so');lib.write_bytes(b'library-v1');libs.append(lib)
  ldd='\n'.join(p.name+' => '+str(p) for p in libs)
  with patch('snapshot_cache.subprocess.check_output',return_value=ldd):
   def prepare(mode='auto'):
    cache=SnapshotCache(root,exe,mode);cache.prepare();return cache
   def publish(cache):
    for name in cache.expected():(cache.directory/name).write_bytes(b'snapshot-v1')
    cache.finish(True)
   assert prepare('off').info['action']=='off'
   cache=prepare();assert cache.info['action']=='write';assert not (cache.base/'active.json').exists();publish(cache)
   hit=prepare();assert hit.info['action']=='read'
   active=(cache.base/'active.json').read_bytes()
   forced=prepare('rebuild');assert forced.info['action']=='write';forced.finish(False)
   assert (cache.base/'active.json').read_bytes()==active
   # Same-size corruption must invalidate through changed ctime/stamp + SHA.
   snapshot=hit.directory/'head.snapshot';snapshot.write_bytes(b'snapshot-v2')
   bad=prepare();assert bad.info['action']=='write';publish(bad)
   weights=root/'runtime/decoder_00_c512_ZG.raw';weights.write_bytes(b'weights-v2')
   stale=prepare();assert stale.info['action']=='write';publish(stale)
   libs[0].write_bytes(b'library-v2');stale=prepare();assert stale.info['action']=='write';publish(stale)
   exe.write_bytes(b'executable-v2');stale=prepare();assert stale.info['action']=='write';publish(stale)
   manifest=json.loads((stale.base/'active.json').read_text());assert len(manifest['snapshots'])==25
   (stale.directory/'head.snapshot').unlink();missing=prepare();assert missing.info['action']=='write';missing.finish(False)
   stale.invalidate();assert not (stale.base/'active.json').exists()
   assert len(list(stale.base.glob('g-*')))<=2
 print(json.dumps({'passed':True,'scope':'off, miss, hit, atomic failure, corruption, model/library/executable changes, missing file, invalidation and bounded generations; no hardware claim'}))

if __name__=='__main__':main()
