from pathlib import Path
import argparse,time,json,importlib.util
import torch,numpy as np
from transformers import AutoModelForCausalLM,AutoTokenizer
p=argparse.ArgumentParser();p.add_argument('--full',action='store_true');a=p.parse_args()
r=Path(__file__).resolve().parents[1];torch.set_num_threads(4)
spec=importlib.util.spec_from_file_location('export',r/'tools/export_qwen.py')
# Import module without exporting graphs.
import sys;sys.argv=['export_qwen.py','--layers','','--context','512'];e=importlib.util.module_from_spec(spec);spec.loader.exec_module(e)
m=AutoModelForCausalLM.from_pretrained('/project/models/Qwen2.5-0.5B-Instruct',torch_dtype=torch.float32,attn_implementation='eager').eval();t=AutoTokenizer.from_pretrained('/project/models/Qwen2.5-0.5B-Instruct')
with torch.inference_mode():
 ids=torch.tensor([[9707]]);ref=m.generate(ids,max_new_tokens=4,do_sample=False,pad_token_id=151643)
 (r/'results/reference_generation.json').write_text(json.dumps({'prompt':[9707],'expected_ids':ref[0,1:].tolist(),'text':t.decode(ref[0,1:])},ensure_ascii=False,indent=2));print('reference',ref.tolist(),flush=True)
 # Nonzero position and populated cache: compare against HF one actual layer.
 x=torch.randn(1,1,896)*.05;past_k=torch.randn(2,512,64)*.1;past_v=torch.randn(2,512,64)*.1;position=7
 f=torch.arange(0,64,2).float()/64;angle=position/(1000000**f);angle=torch.cat((angle,angle));cos=angle.cos();sin=angle.sin()
 packed=torch.zeros(1,1,2073,64);v=packed.reshape(-1);v[:896]=x.reshape(-1);v[896:960]=cos;v[960:1024]=sin;v[1024:1536]=-10000;v[1024:1031]=0;v[1536]=0;off=25*64;v[off:off+65536]=past_k.reshape(-1);v[off+65536:off+131072]=past_v.reshape(-1)
 from transformers.cache_utils import DynamicCache
 cache=DynamicCache();cache.update(past_k[:,:position].unsqueeze(0),past_v[:,:position].unsqueeze(0),0)
 original=m.model.layers[0](x,attention_mask=torch.zeros(1,1,1,position+1),position_ids=torch.tensor([[position]]),position_embeddings=(cos.reshape(1,1,64),sin.reshape(1,1,64)),past_key_value=cache,use_cache=True,cache_position=torch.tensor([position]))[0]
 changed=e.Decoder(0).eval()(packed.permute(0,3,1,2))
 np.testing.assert_allclose(changed.reshape(-1)[:896].numpy(),original.reshape(-1).numpy(),atol=2e-4,rtol=2e-4)
 packed.numpy().tofile(r/'results/nonzero_cache_input.ftmp');changed.numpy().tofile(r/'results/nonzero_cache_expected.ftmp')
 (r/'results/rewrite_equivalence.json').write_text(json.dumps({'scope':'real HF layer, position7 with7 cached tokens, float32','max_abs':float((changed.reshape(-1)[:896]-original.reshape(-1)).abs().max()),'passed':True},indent=2))
