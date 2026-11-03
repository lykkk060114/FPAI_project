#!/usr/bin/env python3
import argparse,subprocess,time,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('name');p.add_argument('--shape',default='1,16,896,1');a=p.parse_args()
r=Path(__file__).resolve().parents[1];base='/project/'+str(r.relative_to(Path(__file__).resolve().parents[4]));out=r/'models'/a.name;out.mkdir(exist_ok=True);rows=[]
def run(cmd,stage):
 t=time.perf_counter()
 with (out/(stage+'.log')).open('w') as f: z=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT)
 rows.append({'stage':stage,'seconds':time.perf_counter()-t,'exit_code':z.returncode});(out/'timing.json').write_text(json.dumps(rows,indent=2))
 if z.returncode:raise SystemExit(stage+' failed: '+str(out/(stage+'.log')))
c=base+'/models/'+a.name
run(['docker','exec','fmql-icraft-convert-20261012','icraft-parse','--net_name',a.name,'--network',base+'/models/'+a.name+'.onnx','--framework','ONNX','--target','Zhuge','--inputs',a.shape,'--inputs_layout','NHWC','--inputs_dtype','FP32','--pre_method','resize','--pre_scale',','.join(['1']*int(a.shape.split(',')[-1])),'--pre_mean',','.join(['0']*int(a.shape.split(',')[-1])),'--channel_swap',','.join(str(i) for i in range(int(a.shape.split(',')[-1]))),'--jr_path',c+'/parsed'],'parse')
for stage,prev,suffix in [('optimize','parsed','parsed'),('quantize','optimize','optimized'),('adapt','quantize','quantized')]:
 cmd=['docker','exec','fmql-icraft-convert-20261012','icraft-'+stage,'--json',c+'/'+prev+'/'+a.name+'_'+suffix+'.json','--raw',c+'/'+prev+'/'+a.name+'_'+suffix+'.raw','--target','ZHUGE','--jr_path',c+'/'+stage]
 if stage=='quantize':cmd+=['--qdtype','bf16']
 run(cmd,stage)
cmd='cd /tmp/icraft-win-bin && export WINEPREFIX=/tmp/fmql-wine WINEDEBUG=-all && xvfb-run -a /usr/lib/wine/wine64 icraft-generate.exe --json Z:'+c+'/adapt/'+a.name+'_adapted.json --raw Z:'+c+'/adapt/'+a.name+'_adapted.raw --jr_path Z:'+c+'/generated'
run(['docker','exec','fmql-windows-codegen-20261013','bash','-lc',cmd],'generate')
d=json.loads((out/'generated'/f'{a.name}_ZG.json').read_text());from collections import Counter
run(['docker','exec','fmql-windows-codegen-20261013','chmod','-R','a+rwX',c],'permissions')
print(dict(Counter(o.get('compile_target') for o in d['ops'])))
print([(o['op_id'],o['_type_key']) for o in d['ops'] if o.get('compile_target')=='@hostt'])
