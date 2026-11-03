from pathlib import Path
import torch,onnx,json
p=Path(__file__).resolve().parents[1];torch.set_num_threads(4)
class Rms(torch.nn.Module):
 def forward(self,x):
  x=x.reshape(1,16,896)
  y=torch.cat((x,-x),dim=-1)
  return torch.nn.functional.layer_norm(y,(1792,),eps=1e-6)[...,:896].reshape(1,16,896,1)
m=Rms().eval();x=torch.randn(1,16,896,1)
with torch.no_grad():
 a=m(x);b=x*torch.rsqrt(x.square().mean(dim=2,keepdim=True)+1e-6)
 print('max_abs',float((a-b).abs().max()))
 torch.onnx.export(m,x,p/'models/rms.onnx',opset_version=17,input_names=['x'],output_names=['y'])
 x.numpy().tofile(p/'results/rms_input.ftmp');b.numpy().tofile(p/'results/rms_expected.ftmp')
