#!/usr/bin/env python3
import importlib.util,json,tempfile,sys,os
from pathlib import Path
r=Path(__file__).resolve().parents[1];s=importlib.util.spec_from_file_location('runner',r/'tools/generate_summary_zg330.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'statistics.json').write_text(json.dumps({'days':{'test':{'target_observed_seconds':0}}}));sys.argv=['runner','--session',d];assert m.main()==0;assert json.loads((p/'summary.json').read_text())['status']=='insufficient_observation'
 (p/'statistics.json').write_text(json.dumps({'days':{'test':{'target_observed_seconds':300,'actions_seconds':{'eat':120,'rest':150,'walk':20,'drink':10},'unknown_behavior_seconds':0}}}));sys.argv=['runner','--session',d,'--max-new-tokens','0'];assert m.main()==1;assert json.loads((p/'summary.json').read_text())['status']=='generation_failed'
 # Stub the compute executable, preserving real tokenizer/prompt/JSON plumbing.
 from tokenizers import Tokenizer
 tokenizer=Tokenizer.from_file(str(r/'runtime/tokenizer.json'))
 answer='熊猫在300秒内进食120秒，休息150秒，行走20秒，喝水10秒，未知动作0秒。'
 answer_ids=tokenizer.encode(answer,add_special_tokens=False).ids+[151645]
 stub=p/'stub';stub.write_text(chr(10).join(['#!/usr/bin/env python3','import sys','a=sys.argv',"open(a[a.index('--output')+1],'w').write("+repr('\n'.join(map(str,answer_ids))+'\n')+")",'']));stub.chmod(0o755)
 sys.argv=['runner','--session',d,'--backend','host','--executable',str(stub),'--max-new-tokens','8'];assert m.main()==0
 result=json.loads((p/'summary.json').read_text());assert result['backend']=='iCraft Host instruction simulation';assert result['stop_reason']=='eos';assert (p/'qwen_prompt.ids').exists()
 record={'observed_s':300.,'actions_s':{'eat':120.,'rest':150.,'walk':20.,'drink':10.},'unknown_s':0.}
 assert m.validate_text(answer,record)['passed']
 assert not m.validate_text('观察300秒，占全天85.22%，表现良好。',record)['passed']
 assert not m.validate_text(answer.replace('120秒','121秒'),record)['passed']
 assert not m.validate_text(answer.replace('，未知动作0秒',''),record)['passed']
 # A broken snapshot must retry ordinary ZG330 loading, preserving diagnostics.
 class FailedCache:
  def __init__(self,*args):self.info={'action':'read'};self.invalidated=False;m.last_cache=self
  def prepare(self):return ['--snapshot-mode','read']
  def finish(self,success):self.finished=success
  def invalidate(self):self.invalidated=True
 m.SnapshotCache=FailedCache
 stub.write_text(stub.read_text().replace('a=sys.argv','a=sys.argv\nif "--snapshot-mode" in a:raise SystemExit(17)'))
 sys.argv=['runner','--session',d,'--backend','zg330','--executable',str(stub),'--max-new-tokens','8'];assert m.main()==0
 result=json.loads((p/'summary.json').read_text());assert result['backend']=='iCraft ZG330'
 assert m.last_cache.invalidated and not m.last_cache.finished
 assert result['snapshot_cache']['fallback']=='ordinary_zg330_loading_after_snapshot_failure'
 assert (p/'qwen_runtime_snapshot_failed.log').exists()
 # A successful cache build is published independently of text-quality checks.
 class WriteCache(FailedCache):
  def prepare(self):self.info={'action':'write'};return []
 m.SnapshotCache=WriteCache
 sys.argv=['runner','--session',d,'--backend','zg330','--executable',str(stub),'--max-new-tokens','8'];assert m.main()==0
 assert m.last_cache.finished and not m.last_cache.invalidated
 print(json.dumps({'tests':9,'passed':True,'scope':'wrapper plumbing, factual guards, cache fallback/publication with fake compute; no hardware claim'}))
