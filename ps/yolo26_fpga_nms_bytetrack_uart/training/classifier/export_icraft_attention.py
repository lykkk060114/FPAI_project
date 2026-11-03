#!/usr/bin/env python3
"""Export fixed-batch DeiT attention without rank-5 transpose or Split."""
import argparse, copy, json, types
from pathlib import Path
from collections import Counter
import numpy as np
import torch
import timm
import onnx
import onnxruntime as ort
from timm.layers import set_fused_attn

def per_head(self, x, attn_mask=None, is_causal=False):
    if attn_mask is not None or is_causal:
        raise ValueError('Only unmasked DeiT self-attention is supported')
    qkv = self.qkv(x)
    c = self.attn_dim
    heads = []
    for h in range(self.num_heads):
        lo, hi = h * self.head_dim, (h + 1) * self.head_dim
        q = qkv[:, :, lo:hi]
        k = qkv[:, :, c + lo:c + hi]
        v = qkv[:, :, 2*c + lo:2*c + hi]
        scores = (q * self.scale) @ k.transpose(1, 2)
        heads.append(self.attn_drop(scores.softmax(dim=-1)) @ v)
    return self.proj_drop(self.proj(self.norm(torch.cat(heads, dim=-1))))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('checkpoint', type=Path)
    ap.add_argument('original', type=Path)
    ap.add_argument('output', type=Path)
    args = ap.parse_args()
    torch.set_num_threads(4)
    torch.manual_seed(20261023)
    set_fused_attn(False)
    ck = torch.load(args.checkpoint, map_location='cpu', weights_only=True)
    original = timm.create_model(ck['architecture'], pretrained=False, num_classes=len(ck['classes']))
    original.load_state_dict(ck['model']); original.eval()
    model = copy.deepcopy(original)
    for block in model.blocks:
        a = block.attn
        assert isinstance(a.q_norm, torch.nn.Identity) and isinstance(a.k_norm, torch.nn.Identity)
        assert a.gate is None
        a.forward = types.MethodType(per_head, a)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    dummy = torch.zeros(1,3,224,224)
    torch.onnx.export(model, dummy, args.output, input_names=['images'], output_names=['logits'], opset_version=13, dynamic_axes=None, do_constant_folding=True)
    graph = onnx.load(args.output); onnx.checker.check_model(graph)
    opts = ort.SessionOptions(); opts.intra_op_num_threads=4
    old = ort.InferenceSession(str(args.original), opts, providers=['CPUExecutionProvider'])
    new = ort.InferenceSession(str(args.output), opts, providers=['CPUExecutionProvider'])
    rows=[]
    with torch.inference_mode():
        for i in range(8):
            x = dummy if i==0 else torch.randn_like(dummy)
            expected=original(x).numpy(); changed=model(x).numpy()
            a=old.run(None,{old.get_inputs()[0].name:x.numpy()})[0]
            b=new.run(None,{new.get_inputs()[0].name:x.numpy()})[0]
            np.testing.assert_allclose(changed,expected,rtol=1e-4,atol=2e-5)
            np.testing.assert_allclose(b,a,rtol=1e-4,atol=2e-5)
            rows.append({'case':i,'torch_max_abs':float(np.max(np.abs(changed-expected))),'onnx_max_abs':float(np.max(np.abs(b-a))),'argmax_equal':bool(np.argmax(a)==np.argmax(b))})
    report={'scope':'8 synthetic fixed-seed inputs; numerical equivalence only, not dataset accuracy or NPU verification','onnx_ops':dict(Counter(n.op_type for n in graph.graph.node)),'cases':rows}
    args.output.with_suffix('.equivalence.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))
if __name__=='__main__': main()
