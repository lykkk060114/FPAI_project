#!/usr/bin/env python3
"""Post-detection Qwen generation. Explicit backend; no silent CPU fallback."""
import argparse,csv,fcntl,json,os,platform,re,subprocess,time
from decimal import Decimal
from snapshot_cache import SnapshotCache
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def atomic(path,data):
 tmp=path.with_suffix('.pending');tmp.write_text(json.dumps(data,ensure_ascii=False,indent=2));tmp.replace(path)
def summary_prompt(record):
 labels={'eat':'进食','rest':'休息','walk':'行走','drink':'喝水'}
 # Give a complete factual sentence to preserve label-number alignment. The
 # small model must not add durations (unknown time is already in observed).
 sentence=('观察时长%.1f秒；'%record['observed_s']+'；'.join('%s%.1f秒'%(label,record['actions_s'][key]) for key,label in labels.items())+
           '；未知动作%.1f秒。'%record['unknown_s'])
 return '请原样输出下面这条观察记录，不加解释，不计算总和，不改变行为名称或数字：\n'+sentence
def validate_text(text,record):
 errors=[]
 if not text.strip():errors.append('empty_text')
 if re.search(r'[%％]|百分之|良好|健康|正常|异常|活跃|疾病|情绪|精神',text):errors.append('unsupported_ratio_or_assessment')
 expected={Decimal(str(v)) for v in [record['observed_s'],record['unknown_s']]+list(record['actions_s'].values())}
 actual=[Decimal(x) for x in re.findall(r'\d+(?:\.\d+)?',text)]
 if any(x not in expected for x in actual):errors.append('unexpected_numeric_value')
 if Decimal(str(record['observed_s'])) not in actual:errors.append('missing_observed_duration')
 for label,value in [('进食',record['actions_s']['eat']),('休息',record['actions_s']['rest']),('行走',record['actions_s']['walk']),('喝水',record['actions_s']['drink']),('未知(?:动作|行为)',record['unknown_s'])]:
  found=re.findall(label+r'[^\d，。；]{0,8}(\d+(?:\.\d+)?)\s*秒',text)
  if not found or any(Decimal(x)!=Decimal(str(value)) for x in found):errors.append('missing_or_incorrect_duration:'+label)
 return {'passed':not errors,'errors':errors,'scope':'numeric and prohibited-phrase checks; not general semantic accuracy'}
def main():
 p=argparse.ArgumentParser();p.add_argument('--session',required=True,type=Path);p.add_argument('--backend',choices=['zg330','host'],default='zg330');p.add_argument('--executable',type=Path);p.add_argument('--max-new-tokens',type=int,default=96);p.add_argument('--timeout',type=int,default=3600);mode=p.add_mutually_exclusive_group();mode.add_argument('--resident',dest='resident',action='store_true');mode.add_argument('--streaming',dest='resident',action='store_false');p.set_defaults(resident=True);p.add_argument('--prefill-block',type=int,choices=[1,16],default=1);p.add_argument('--snapshot-mode',choices=['auto','off','rebuild'],default='auto');a=p.parse_args()
 session=a.session.resolve();session.mkdir(parents=True,exist_ok=True);result={'model':'Qwen2.5-0.5B-Instruct','backend':'iCraft ZG330' if a.backend=='zg330' else 'iCraft Host instruction simulation','quantization':'BF16','health_status':'not_assessable_from_behavior_only','clinical_conclusion':'仅凭行为分类和观察时长不能判断熊猫是否健康，需结合个体基线及饲养员、兽医评估。','execution_architecture':platform.machine(),'generated_text_is_not_a_validated_health_assessment':True}
 start=time.perf_counter()
 try:
  if a.max_new_tokens<1 or a.max_new_tokens>256 or a.timeout<1:raise ValueError('invalid generation limit')
  lock=os.fdopen(os.open(str(ROOT/'generation.lock'),os.O_RDONLY|os.O_CREAT,0o644),'r');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
  for proc in Path('/proc').glob('[0-9]*'):
   try:
    args=(proc/'cmdline').read_bytes().split(b'\0')
    if args and Path(os.fsdecode(args[0])).name.startswith('sdicamera+'):raise RuntimeError('detection process still running; Qwen must start after it exits')
   except (OSError,ValueError):pass
  report=json.loads((session/'statistics.json').read_text());days=report.get('days',{})
  if sum(b['target_observed_seconds'] for b in days.values())<=0:
   result.update(status='insufficient_observation',text='没有有效目标观察时长。');atomic(session/'summary.json',result);return 0
  from tokenizers import Tokenizer
  tokenizer=Tokenizer.from_file(str(ROOT/'runtime/tokenizer.json'))
  supplied=json.loads((session/'summary_input.json').read_text()) if (session/'summary_input.json').exists() else {'scope':'session','days':days}
  records=[]
  for day,b in sorted(supplied['days'].items()):
   records.append({'date':day,'observed_s':round(b['target_observed_seconds'],1),'actions_s':{k:round(v,1) for k,v in b['actions_seconds'].items()},'unknown_s':round(b['unknown_behavior_seconds'],1)})
  if len(records)!=1:raise ValueError('generate one calendar day per summary; no silent day merging')
  prompt=summary_prompt(records[0])
  result.update(summary_scope=supplied['scope'],summary_records=records)
  chat='<|im_start|>system\n你是熊猫行为记录助手，只描述数据，不作健康诊断。<|im_end|>\n<|im_start|>user\n'+prompt+'<|im_end|>\n<|im_start|>assistant\n'
  ids=tokenizer.encode(chat,add_special_tokens=False).ids
  if len(ids)+a.max_new_tokens>512:raise ValueError('statistics prompt+generation exceeds context512; split by day; no truncation performed')
  (session/'qwen_prompt.txt').write_text(chat);(session/'qwen_prompt.ids').write_text('\n'.join(map(str,ids))+'\n')
  executable=a.executable or ROOT/'bin/qwen_icraft';command=[str(executable),'--models',str(ROOT/'runtime'),'--tokens',str(session/'qwen_prompt.ids'),'--output',str(session/'qwen_generated.ids'),'--timing',str(session/'qwen_stage_timing.csv'),'--backend',a.backend,'--max-new-tokens',str(a.max_new_tokens)]
  command.extend(['--prefill-block',str(a.prefill_block)])
  if a.resident:command.append('--resident')
  snapshot_mode=a.snapshot_mode if a.backend=='zg330' and a.resident and a.prefill_block==1 else 'off'
  if a.snapshot_mode=='rebuild' and snapshot_mode=='off':raise ValueError('snapshot rebuild requires zg330, resident and prefill-block 1')
  cache=SnapshotCache(ROOT,executable,snapshot_mode)
  snapshot_args=cache.prepare();result['snapshot_cache']=cache.info
  runtime_start=time.perf_counter()
  def invoke(args):
   with (session/'qwen_runtime.log').open('w') as log:
    return subprocess.run(command+args,stdout=log,stderr=subprocess.STDOUT,timeout=max(1,a.timeout-(time.perf_counter()-runtime_start)))
  try:run=invoke(snapshot_args)
  except BaseException:
   cache.finish(False);raise
  cache.finish(run.returncode==0)
  if run.returncode!=0 and cache.info['action']=='read':
   cache.invalidate()
   for name in ('qwen_runtime.log','qwen_stage_timing.csv','qwen_generated.ids'):
    file=session/name
    if file.exists():file.replace(session/(file.stem+'_snapshot_failed'+file.suffix))
   cache.info['fallback']='ordinary_zg330_loading_after_snapshot_failure'
   print('[QWEN] Snapshot restore failed; retrying ordinary ZG330 loading',flush=True)
   run=invoke([])

  generated=[int(x) for x in (session/'qwen_generated.ids').read_text().split()] if (session/'qwen_generated.ids').exists() else []
  text=tokenizer.decode(generated,skip_special_tokens=True);(session/'qwen_output.txt').write_text(text)
  eos=bool(generated and generated[-1] in (151643,151645))
  result.update(status=('generated_unreviewed' if eos else 'length_limited_unreviewed') if run.returncode==0 else 'generation_failed',exit_code=run.returncode,text=text,prompt_tokens=len(ids),generated_tokens=len(generated),stop_reason='eos' if eos else 'length_or_error',cpu_responsibilities=['tokenization','embedding table lookup','KV storage and packing','graph scheduling and tensor transfers','greedy argmax and text decoding'],npu_responsibilities=['RMSNorm equivalent LayerNorm','Q/K/V and output projections','RoPE application','attention matmul/softmax','SwiGLU MLP','final norm and vocabulary projection'])
  result['content_validation']=validate_text(text,records[0])
  accepted=run.returncode==0 and eos and result['content_validation']['passed']
  if not accepted:
   result.update(status='generation_rejected',rejected_text=text,text='',error='Output did not pass completion and factual checks; raw text retained in qwen_output.txt')
  result['memory_strategy']={'resident':a.resident,'prefill_block':a.prefill_block,'host_weight_mapping':'read-only mmap; clean host pages discarded after device upload; validated session snapshots used when available'}
  result['timing']={'total_wall_seconds':time.perf_counter()-start,'stage_csv':str(session/'qwen_stage_timing.csv')}
  atomic(session/'summary.json',result);print('[QWEN '+a.backend+'] '+result['status']+': '+str(session/'summary.json'),flush=True);return 0 if accepted else 1
 except subprocess.TimeoutExpired:result.update(status='generation_timeout')
 except Exception as e:result.update(status='generation_failed',error=str(e))
 result['timing']={'total_wall_seconds':time.perf_counter()-start};atomic(session/'summary.json',result);print('[QWEN] '+result['status']+': '+result.get('error','timeout'),flush=True);return 1
if __name__=='__main__':raise SystemExit(main())
