from pathlib import Path
import sys,importlib.util,json,torch,numpy as np
from transformers import AutoModelForCausalLM
from transformers.cache_utils import DynamicCache
r=Path(__file__).resolve().parents[1];torch.set_num_threads(4)
sys.argv=['export_qwen.py','--layers','','--context','512','--block','16'];s=importlib.util.spec_from_file_location('export',r/'tools/export_qwen.py');e=importlib.util.module_from_spec(s);s.loader.exec_module(e)
m=AutoModelForCausalLM.from_pretrained('/project/models/Qwen2.5-0.5B-Instruct',torch_dtype=torch.float32,attn_implementation='eager').eval()
B=16;C=512;valid=13;position=7;torch.manual_seed(123);x=torch.randn(1,B,896)*.05;k=torch.randn(2,C,64)*.1;v=torch.randn(2,C,64)*.1
positions=torch.arange(position,position+B);inv=1/(1000000**(torch.arange(0,64,2).float()/64));freq=positions[:,None]*inv[None,:];freq=torch.cat((freq,freq),dim=-1);cos=freq.cos();sin=freq.sin()
packed=torch.zeros(1,1,2448,64);a=packed.reshape(-1);a[:B*896]=x.reshape(-1);a[B*896:B*960]=cos.reshape(-1);a[B*960:B*1024]=sin.reshape(-1)
mask=a[1024*B:1024*B+9*64*B].reshape(B,576);mask[:]=-10000
for b in range(B):mask[b,:position]=0;mask[b,C:C+b+1]=0
off=(16*B+9*B)*64;a[off:off+65536]=k.reshape(-1);a[off+65536:off+131072]=v.reshape(-1)
cache=DynamicCache();cache.update(k[:,:position].unsqueeze(0),v[:,:position].unsqueeze(0),0)
attention=torch.full((1,1,valid,position+valid),-10000.)
for b in range(valid):attention[:,:,b,:position+b+1]=0
with torch.inference_mode():
 expected=m.model.layers[0](x[:,:valid],attention_mask=attention,position_ids=positions[None,:valid],position_embeddings=(cos[None,:valid],sin[None,:valid]),past_key_value=cache,use_cache=True,cache_position=positions[:valid])[0]
 actual=e.Decoder(0).eval()(packed.permute(0,3,1,2)).reshape(-1)
 newhidden=actual[:B*896].reshape(1,B,896)[:,:valid]
 np.testing.assert_allclose(newhidden.numpy(),expected.numpy(),atol=3e-4,rtol=3e-4)
 packed.numpy().tofile(r/'results/prefill_cache_input.ftmp');actual.numpy().tofile(r/'results/prefill_cache_expected.ftmp')
 report={'passed':True,'scope':'FP32 real HF layer0, position7 past7 and13 valid tokens in padded16 block','max_abs':float((newhidden-expected).abs().max())};(r/'results/prefill_equivalence.json').write_text(json.dumps(report,indent=2));print(report)
