#!/usr/bin/env python3
"""Real Qwen2.5 weights; fixed-size incremental KV cache, no CPU RMSNorm."""
import argparse,json,time,math
from pathlib import Path
import torch,numpy as np,onnx
from safetensors import safe_open
p=argparse.ArgumentParser();p.add_argument('--layers',default='0');p.add_argument('--context',type=int,default=256);p.add_argument('--head',action='store_true');p.add_argument('--block',type=int,default=1);a=p.parse_args()
r=Path(__file__).resolve().parents[1];root=Path('/project/models/Qwen2.5-0.5B-Instruct');torch.set_num_threads(4);torch.manual_seed(20261026)
f=safe_open(root/'model.safetensors',framework='pt',device='cpu');C=a.context;D=896;H=64;B=a.block
def weight(name):return f.get_tensor(name).float()
def norm(x,w):
 y=torch.cat((x,-x),dim=-1)
 return torch.nn.functional.layer_norm(y,(1792,),eps=1e-6)[...,:896]*w
def rotate(x,cos,sin):
 return x*cos+torch.cat((-x[...,32:],x[...,:32]),dim=-1)*sin
class Decoder(torch.nn.Module):
 def __init__(self,index):
  super().__init__();prefix=f'model.layers.{index}.'
  for local,key in [('n1','input_layernorm.weight'),('n2','post_attention_layernorm.weight')]:self.register_buffer(local,weight(prefix+key))
  for name in ['q_proj','k_proj','v_proj','o_proj']:
   self.register_buffer(name+'_w',weight(prefix+'self_attn.'+name+'.weight'))
   if name!='o_proj':self.register_buffer(name+'_b',weight(prefix+'self_attn.'+name+'.bias'))
  for name in ['gate_proj','up_proj','down_proj']:self.register_buffer(name+'_w',weight(prefix+'mlp.'+name+'.weight'))
 def forward(self,packed):
  packed=packed.permute(0,2,3,1)
  x=packed[:,:,:14*B,:].reshape(1,B,896)
  cos=packed[:,:,14*B:15*B,:].reshape(1,B,64)
  sin=packed[:,:,15*B:16*B,:].reshape(1,B,64)
  rows=(C+B+63)//64;offset=16*B+rows*B
  mask=packed[:,:,16*B:offset,:].reshape(1,B,rows*64)[:,:,:C+B]
  keys=packed[:,:,offset:offset+2*C,:].reshape(2,C,64);offset+=2*C
  vals=packed[:,:,offset:offset+2*C,:].reshape(2,C,64)
  y=norm(x,self.n1)
  q=rotate(torch.nn.functional.linear(y,self.q_proj_w,self.q_proj_b).reshape(1,B,14,64).reshape(B,14,64).transpose(0,1),cos,sin)
  k=rotate(torch.nn.functional.linear(y,self.k_proj_w,self.k_proj_b).reshape(1,B,2,64).reshape(B,2,64).transpose(0,1),cos,sin)
  val=torch.nn.functional.linear(y,self.v_proj_w,self.v_proj_b).reshape(1,B,2,64).reshape(B,2,64).transpose(0,1)
  heads=[]
  for h in range(14):
   g=h//7;ks=torch.cat((keys[g:g+1],k[g:g+1]),dim=1);vs=torch.cat((vals[g:g+1],val[g:g+1]),dim=1)
   scores=(q[h:h+1]*(1/8))@ks.transpose(1,2)+mask
   heads.append(scores.softmax(dim=-1)@vs)
  attended=torch.cat(heads,dim=-1).reshape(1,B,896)
  x=x+torch.nn.functional.linear(attended,self.o_proj_w)
  y=norm(x,self.n2)
  y=torch.nn.functional.silu(torch.nn.functional.linear(y,self.gate_proj_w))*torch.nn.functional.linear(y,self.up_proj_w)
  x=x+torch.nn.functional.linear(y,self.down_proj_w)
  return torch.cat((x.reshape(-1),k.reshape(-1),val.reshape(-1))).reshape(1,1,B*1152,1)
class Head(torch.nn.Module):
 def __init__(self):
  super().__init__();self.register_buffer('n',weight('model.norm.weight'));self.register_buffer('w',weight('model.embed_tokens.weight'))
 def forward(self,x):return torch.nn.functional.linear(norm(x.reshape(1,1,896),self.n),self.w).reshape(1,1,151936,1)
size=(16*B+((C+B+63)//64)*B+4*C)*64
for i in [int(x) for x in a.layers.split(',') if x]:
 m=Decoder(i).eval();v=torch.zeros(1,1,size//64,64);v.reshape(-1)[:896*B]=torch.randn(896*B)*.05;v.reshape(-1)[896*B:960*B]=1
 name=f'decoder_{i:02d}_c{C}'+(f'_b{B}' if B!=1 else '');start=time.perf_counter()
 with torch.no_grad():
  v=v.permute(0,3,1,2).contiguous();expected=m(v);v.permute(0,2,3,1).contiguous().numpy().tofile(r/'results'/f'{name}_input.ftmp');expected.numpy().tofile(r/'results'/f'{name}_expected.ftmp')
  torch.onnx.export(m,v,r/'models'/f'{name}.onnx',opset_version=17,input_names=['packed_state'],output_names=['hidden_kv'],do_constant_folding=True)
 onnx.checker.check_model(str(r/'models'/f'{name}.onnx'))
 (r/'results'/f'{name}_export.json').write_text(json.dumps({'real_checkpoint':str(root),'context':C,'input_elements':size,'output_elements':B*1152,'seconds':time.perf_counter()-start}))
 print(name,'exported',flush=True)
if a.head:
 m=Head().eval();x=torch.randn(1,1,896,1)*.05
 with torch.no_grad():
  x.numpy().tofile(r/'results/head_input.ftmp');m(x).numpy().tofile(r/'results/head_expected.ftmp')
  torch.onnx.export(m,x,r/'models/head.onnx',opset_version=17,input_names=['hidden'],output_names=['logits'],do_constant_folding=True)
 print('head exported',flush=True)
(r/'models'/('config.json' if B==1 else 'prefill_config.json')).write_text(json.dumps({'architecture':'Qwen2.5-0.5B-Instruct','layers':24,'context':C,'hidden':896,'kv_heads':2,'head_dim':64,'vocab':151936,'input_elements':size,'output_elements':B*1152,'rms_rewrite':'LayerNorm(concat(x,-x))[:896] * weight','sampling':'CPU greedy; no CPU neural compute intended','weights':'pretrained model.safetensors'},indent=2))
