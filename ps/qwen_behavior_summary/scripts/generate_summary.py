#!/usr/bin/env python3
"""Genuine pretrained Qwen fallback. CPU backend, only after detector exits."""
import argparse,json,os,subprocess,time,platform
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def save(path,obj):
 tmp=path.with_suffix('.pending');tmp.write_text(json.dumps(obj,ensure_ascii=False,indent=2));os.replace(str(tmp),str(path))
def main():
 settings_path=ROOT/'settings.json'
 settings=json.loads(settings_path.read_text()) if settings_path.exists() else {}
 if not settings.get('enabled',True):
  import sys
  parser=argparse.ArgumentParser();parser.add_argument('--session',required=True,type=Path);args=parser.parse_args()
  save(args.session/'summary.json',dict(status='disabled_pending_hardware_validation',backend=None,text='CPU总结已停用；ZG330完整生成尚待验收。'))
  print('[QWEN] Disabled pending hardware validation; no CPU model started',flush=True)
  return 0
 selected=os.environ.get("XUNJING_QWEN_BACKEND",settings.get('backend','auto'))
 if selected not in ("auto","cpu","zg330"):raise SystemExit("XUNJING_QWEN_BACKEND must be auto/cpu/zg330")
 zgroot=ROOT/"zg330"
 ready=platform.machine() in ('aarch64','arm64') and (zgroot/"runtime/config.json").exists() and (zgroot/"bin/qwen_icraft").exists()
 if selected=="zg330" or (selected=="auto" and ready):
  import sys
  return subprocess.call([sys.executable,str(zgroot/"tools/generate_summary_zg330.py")]+sys.argv[1:])
 p=argparse.ArgumentParser();p.add_argument('--session',required=True,type=Path);p.add_argument('--timeout',type=int,default=300);p.add_argument('--executable',type=Path,default=ROOT/'bin/llama-cli');a=p.parse_args();session=a.session
 result=dict(model='Qwen2.5-0.5B-Instruct',quantization='Q4_K_M',backend='llama.cpp ARM CPU',health_status='not_assessable_from_behavior_only',clinical_conclusion='仅凭行为分类和观察时长不能判断熊猫是否健康，需结合长期个体基线及饲养员、兽医评估。')
 report=json.loads((session/'statistics.json').read_text())
 if sum(b['target_observed_seconds'] for b in report['days'].values())<=0:
  result.update(status='insufficient_observation',text='没有有效目标观察时长，不能生成行为或健康结论。');save(session/'summary.json',result);return 0
 for proc in Path('/proc').glob('[0-9]*'):
  try:
   args=(proc/'cmdline').read_bytes().split(b'\0')
   if args and Path(os.fsdecode(args[0])).name.startswith('sdicamera+'):
    result.update(status='skipped_inference_running');save(session/'summary.json',result);return 1
  except (OSError,ValueError):pass
 model=ROOT/'models/qwen2.5-0.5b-instruct-q4_k_m.gguf';binary=a.executable
 result['execution_architecture']=platform.machine()
 if not model.exists() or not binary.exists():result.update(status='model_not_deployed');save(session/'summary.json',result);return 1
 prompt=(session/'prompt.txt').read_text();chat='<|im_start|>system\n你是严谨的动物行为记录助手。只根据提供的观察数据描述，不做健康诊断。<|im_end|>\n<|im_start|>user\n'+prompt+'<|im_end|>\n<|im_start|>assistant\n';(session/'qwen_prompt.txt').write_text(chat)
 command=[str(binary),'-m',str(model),'-f',str(session/'qwen_prompt.txt'),'-n','256','-c','2048','-b','128','-ub','128','-t','2','-ngl','0','--temp','0','--seed','0','--no-display-prompt','--simple-io','--no-warmup','-no-cnv']
 started=time.perf_counter()
 try:
  with (session/'qwen_output.txt').open('w') as out,(session/'qwen_runtime.log').open('w') as err:
   run=subprocess.run(command,stdout=out,stderr=err,timeout=a.timeout)
  text=(session/'qwen_output.txt').read_text().replace('[end of text]','').strip()
  result.update(status='generated_unreviewed' if run.returncode==0 else 'generation_failed',exit_code=run.returncode,text=text)
 except subprocess.TimeoutExpired:result.update(status='generation_timeout')
 result['timing']={'total_wall_seconds':time.perf_counter()-started,'runtime_details':str(session/'qwen_runtime.log')};result['generated_text_is_not_a_validated_health_assessment']=True;save(session/'summary.json',result);print('[QWEN] '+result['status']+': '+str(session/'summary.json'),flush=True)
 return 0 if result['status']=='generated_unreviewed' else 1
if __name__=='__main__':raise SystemExit(main())
