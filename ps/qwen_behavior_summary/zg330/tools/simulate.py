#!/usr/bin/env python3
import argparse,json,subprocess,time
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('name');a=p.parse_args();r=Path(__file__).resolve().parents[1];g=r/'models'/a.name/'generated';d=json.loads((g/(a.name+'_ZG.json')).read_text())
shape=json.loads((r/'models'/a.name/'parsed'/(a.name+'_parsed.json')).read_text())
# Match CLI loader to real logical NHWC input.
for op in d['ops']:
 if op['op_id'] in [0,1]:
  for v in op.get('inputs',[])+op.get('outputs',[]):
   if v.get('dtype',{}).get('shape')==[-1,-1,-1,-1]:v['dtype']['shape']=([1,1,2448,64] if a.name.endswith('_b16') else ([1,1,2073,64] if '_c512' in a.name else [1,1,1045,64])) if a.name.startswith('decoder') else [1,1,896,1]
(g/(a.name+'_sim.json')).write_text(json.dumps(d));outid=d['ops'][-1]['inputs'][0]['v_id'];c='/project/'+str(r.relative_to(Path(__file__).resolve().parents[4]));case=c+'/results/'+a.name+'_sim';t=time.perf_counter()
cmd='cd /tmp/icraft-win-bin && export WINEPREFIX=/tmp/fmql-wine WINEDEBUG=-all && xvfb-run -a /usr/lib/wine/wine64 icraft-run.exe --json Z:'+c+'/models/'+a.name+'/generated/'+a.name+'_sim.json --raw Z:'+c+'/models/'+a.name+'/generated/'+a.name+'_ZG.raw --input Z:'+c+'/results/'+a.name+'_input.ftmp --backends Host --dump_output --dump_format SFB --log_path Z:'+case
with (r/'results'/(a.name+'_sim.log')).open('w') as f:subprocess.run(['docker','exec','fmql-windows-codegen-20261013','bash','-lc',cmd],stdout=f,stderr=subprocess.STDOUT,check=True)
files=list((r/'results'/(a.name+'_sim')).rglob(str(outid)+'.ftmp'));actual=np.fromfile(files[0],np.float32);expected=np.fromfile(r/'results'/(a.name+'_expected.ftmp'),np.float32)
report={'scope':'real weights, Host instruction simulation, not board timing','elements':len(actual),'seconds':time.perf_counter()-t,'finite':bool(np.isfinite(actual).all()),'max_abs':float(abs(expected-actual).max()),'rmse':float(np.sqrt(((expected-actual)**2).mean())),'cosine':float(expected@actual/(np.linalg.norm(expected)*np.linalg.norm(actual)))}
(r/'results'/(a.name+'_simulation.json')).write_text(json.dumps(report,indent=2));print(report)
