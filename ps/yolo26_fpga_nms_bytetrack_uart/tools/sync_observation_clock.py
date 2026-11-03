#!/usr/bin/env python3
"""Anchor board monotonic time to host UTC without changing the board clock."""
import argparse,json,subprocess,time
p=argparse.ArgumentParser();p.add_argument('board',nargs='?',default='root@192.168.126.49');a=p.parse_args()
query="python3 - <<'CODE'\nimport json,time\nfrom pathlib import Path\nprint(json.dumps({'steady_ns':time.monotonic_ns(),'boot_id':Path('/proc/sys/kernel/random/boot_id').read_text().strip()}))\nCODE"
start=time.time_ns();out=subprocess.check_output(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=8',a.board,query],text=True);end=time.time_ns();anchor=json.loads(out);anchor['utc_epoch_ns']=(start+end)//2;anchor['utc_uncertainty_ns']=(end-start)//2;anchor['source']='host UTC, SSH round trip midpoint; same boot only'
command="python3 -c \"import sys,pathlib; p=pathlib.Path('/root/workspace/yolo26_fpga_nms_bytetrack_uart/results/observation_clock.json');p.parent.mkdir(parents=True,exist_ok=True);p.write_text(sys.stdin.read())\""
subprocess.run(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=8',a.board,command],input=json.dumps(anchor),text=True,check=True);print(json.dumps(anchor,indent=2))
