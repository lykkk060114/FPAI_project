from pathlib import Path
import json,shutil,hashlib
import torch,numpy as np
from safetensors import safe_open
r=Path(__file__).resolve().parents[1];dest=r/'runtime';dest.mkdir(exist_ok=True)
f=safe_open(str(r.parents[2]/'models/Qwen2.5-0.5B-Instruct/model.safetensors'),framework='pt');embed=f.get_tensor('model.embed_tokens.weight')
embed.view(torch.int16).numpy().tofile(dest/'embedding.bf16')
c=512;inv=1/(1000000**(torch.arange(0,64,2).float()/64));freq=torch.arange(c).float()[:,None]*inv[None,:];freq=torch.cat((freq,freq),dim=-1);torch.cat((freq.cos(),freq.sin()),dim=1).numpy().tofile(dest/'rope.f32')
counts={};host=[]
for name in [f'decoder_{i:02d}_c512' for i in range(24)]+[f'decoder_{i:02d}_c512_b16' for i in range(24)]+['head']:
 src=r/'models'/name/'generated'
 for suffix in ['json','raw']:
  target=dest/(name+'_ZG.'+suffix)
  if target.exists() or target.is_symlink():target.unlink()
  shutil.copy2(src/target.name,target)
 d=json.loads((dest/(name+'_ZG.json')).read_text())
 for op in d['ops']:
  target=op.get('compile_target');counts[target]=counts.get(target,0)+1
  if target=='@hostt' and op['_type_key'] not in ('icraft::xir::Input','icraft::xir::Resize','icraft::xir::Output'):host.append({'model':name,'op':op['_type_key']})
 for op in d['ops']:
  if op['op_id'] in [0,1]:
   for v in op.get('inputs',[])+op.get('outputs',[]):
    if v.get('dtype',{}).get('shape')==[-1,-1,-1,-1]:v['dtype']['shape']=([1,1,2448,64] if name.endswith('_b16') else [1,1,2073,64]) if name!='head' else [1,1,896,1]
 (dest/(name+'_ZG.json')).write_text(json.dumps(d))
for name in ['tokenizer.json','tokenizer_config.json','LICENSE']:shutil.copy2(r.parents[2]/'models/Qwen2.5-0.5B-Instruct'/name,dest/name)
shutil.copy2(r/'models/config.json',dest/'config.json')
report={'real_weights':True,'layers':24,'context':512,'compile_targets':counts,'host_neural_compute':host,'status':'compiled_not_board_validated','runtime_bytes':sum(p.stat().st_size for p in dest.iterdir() if p.is_file()),'normalization':'mathematically equivalent sign-pair LayerNorm; BF16 rounding differs'}
(r/'results/compile_summary.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
assert not host
