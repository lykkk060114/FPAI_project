#!/usr/bin/env python3
"""Reproduce conversion using the installed SDK 3.36.1 containers. No board access."""
from pathlib import Path
import subprocess,json,os
p=Path(__file__).resolve().parents[2];root=p.parents[1]
b=p/'training/classifier/zg330_check_20261023';c='/project/'+str(b.relative_to(root));pkg='/project/'+str(p.relative_to(root))
def run(args,log=None):
 if log:
  with (b/log).open('w') as f:subprocess.run(args,stdout=f,stderr=subprocess.STDOUT,check=True)
 else:subprocess.run(args,check=True)
run(['docker','start','fmql-icraft-convert-20261012','fmql-windows-codegen-20261013'])
run(['docker','run','--rm','-v',str(root)+':/project','xunjing-deit-train:2.5.1','python',pkg+'/training/classifier/export_icraft_attention.py',pkg+'/training/classifier/output_20261012/deit_tiny_panda_behavior_best.pt',pkg+'/imodel/classifier/deit_tiny_panda_behavior.onnx',c+'/deit_per_head.onnx'],'export.log')
def linux(args,log):run(['docker','exec','fmql-icraft-convert-20261012']+args,log)
linux(['icraft-parse','--net_name','deit_per_head','--network',c+'/deit_per_head.onnx','--framework','ONNX','--target','Zhuge','--inputs','1,224,224,3','--inputs_layout','NHWC','--inputs_dtype','FP32','--pre_method','resize','--pre_scale','1,1,1','--pre_mean','0,0,0','--channel_swap','0,1,2','--jr_path',c+'/per_head_parsed'],'per_head_parse.log')
for stage,prev,suffix,dest in [('optimize','per_head_parsed','parsed','optimized'),('quantize','optimized','optimized','quantized'),('adapt','quantized','quantized','adapted')]:
 args=['icraft-'+stage,'--json',c+'/'+prev+'/deit_per_head_'+suffix+'.json','--raw',c+'/'+prev+'/deit_per_head_'+suffix+'.raw','--target','ZHUGE','--jr_path',c+'/'+dest]
 if stage=='quantize':args+=['--qdtype','bf16']
 linux(args,stage+'.log')
cmd='cd /tmp/icraft-win-bin && export WINEPREFIX=/tmp/fmql-wine WINEDEBUG=-all && xvfb-run -a /usr/lib/wine/wine64 icraft-generate.exe --json Z:'+c+'/adapted/deit_per_head_adapted.json --raw Z:'+c+'/adapted/deit_per_head_adapted.raw --jr_path Z:'+c+'/generated_win'
run(['docker','exec','fmql-windows-codegen-20261013','bash','-lc',cmd],'generate_windows.log')
run(['docker','exec','fmql-icraft-convert-20261012','chown','-R',str(os.getuid())+':'+str(os.getgid()),c])
obj=json.loads((b/'generated_win/deit_per_head_ZG.json').read_text())
# Only fix the Host CLI loader's dynamic input; runtime bypasses Host ops 0/1.
for op in obj['ops']:
 if op['op_id'] in [0,1]:
  for value in op.get('inputs',[])+op.get('outputs',[]):
   if value.get('dtype',{}).get('shape')==[-1,-1,-1,-1]:value['dtype']['shape']=[1,224,224,3]
(b/'generated_win/deit_per_head_fixed_input_ZG.json').write_text(json.dumps(obj))
python=os.environ.get('CLASSIFIER_CHECK_PYTHON',str(root.parent/'ai_train/yolov5/.venv/bin/python'))
run([python,str(p/'training/classifier/replay_zg330_classifier.py')],'replay.log')
print('Generation and four-sample simulation completed. Review results before replacing packaged model; no board operations.')
