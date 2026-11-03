#!/usr/bin/env python3
"""Write a machine-local YOLO YAML without changing the portable data.yaml."""
import argparse
import json
from pathlib import Path

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1])
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    data=(args.root.resolve()/'detection')
    for split in ('train','val','test'):
        if not (data/'images'/split).is_dir():parser.error('Missing images/'+split)
    out=args.output.expanduser().resolve()
    if out==data/'data.yaml':parser.error('Use a temporary output path, not tracked detection/data.yaml')
    out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text('path: '+json.dumps(str(data),ensure_ascii=False)+'\ntrain: images/train\nval: images/val\ntest: images/test\nnc: 1\nnames:\n  0: giant_panda\n',encoding='utf-8')
    print(out)
if __name__=='__main__':main()
