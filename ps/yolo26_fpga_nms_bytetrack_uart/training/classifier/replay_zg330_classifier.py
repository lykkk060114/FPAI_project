from pathlib import Path
import numpy as np, cv2, onnxruntime as ort, subprocess,json
p=Path(__file__).resolve().parent / 'zg330_check_20261023'
root=Path(__file__).resolve().parents[4]
sess=ort.InferenceSession(str(root/'ps/yolo26_fpga_nms_bytetrack_uart/imodel/classifier/deit_tiny_panda_behavior.onnx'))
rows=[]
for i,label in enumerate(['drink','eat','rest','walk']):
 im=cv2.imread(str(sorted((root/'datasets/classification/val'/label).glob('*.jpg'))[0])); im=cv2.cvtColor(cv2.resize(im,(224,224)),cv2.COLOR_BGR2RGB).astype(np.float32)/255
 x=(im-np.array([.485,.456,.406],np.float32))/np.array([.229,.224,.225],np.float32)
 case=p/f'sim_{label}';case.mkdir(exist_ok=True);x.tofile(case/'input.ftmp');expected=sess.run(None,{sess.get_inputs()[0].name:x.transpose(2,0,1)[None]})[0].ravel()
 b='/project/'+str(p.relative_to(root)); c=b+'/sim_'+label
 cmd=f'cd /tmp/icraft-win-bin; export WINEPREFIX=/tmp/fmql-wine WINEDEBUG=-all; xvfb-run -a /usr/lib/wine/wine64 icraft-run.exe --json Z:{b}/generated_win/deit_per_head_fixed_input_ZG.json --raw Z:{b}/generated_win/deit_per_head_ZG.raw --input Z:{c}/input.ftmp --backends Host --dump_output --dump_format SFB --dump_ftmp 2,2636 --log_path Z:{c}/dump'
 with (case/'run.log').open('w') as f: subprocess.run(['docker','exec','fmql-windows-codegen-20261013','bash','-lc',cmd],stdout=f,stderr=subprocess.STDOUT,check=True)
 actual=np.fromfile(case/'dump/deit_per_head/ftmpSFB/2636.ftmp',np.float32)
 inp=np.fromfile(case/'dump/deit_per_head/ftmpSFB/2.ftmp',np.float32)
 rows.append(dict(label=label,input_max_abs=float(np.max(np.abs(inp-x.ravel()))),expected=expected.tolist(),actual=actual.tolist(),max_abs=float(np.max(np.abs(expected-actual))),argmax_equal=bool(expected.argmax()==actual.argmax())))
 print(rows[-1],flush=True)
(p/'generated_equivalence.json').write_text(json.dumps({'scope':'Host instruction simulation; four validation images, not board accuracy','cases':rows},indent=2))
