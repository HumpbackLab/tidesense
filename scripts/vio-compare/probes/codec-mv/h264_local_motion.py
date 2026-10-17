#!/usr/bin/env python3
"""Local motion fixtures to distinguish H264 context words from global flow."""
import json
from pathlib import Path
import sys
sys.path.insert(0,'/usr/lib/python3/dist-packages')
import numpy as np

ROOT=Path(__file__).resolve().parents[4]
OUT=ROOT/'install/venc-h264-direct-20261017'

if __name__=='__main__':
    OUT.mkdir(parents=True,exist_ok=True)
    image=np.fromfile(ROOT/'install/venc-mv-device-20261017/shift-8-4.yuv',
                      dtype=np.uint8,count=640*400).reshape(400,640)
    for name,(x,y) in [('bottom-right',(560,320)),('top-left',(0,0))]:
        with (OUT/(name+'.yuv')).open('wb') as f:
            for i in range(12):
                k=max(0,i-3); current=image.copy()
                current[y:y+80,x:x+80]=np.roll(image[y:y+80,x:x+80],(-3*k,-4*k),(0,1))
                f.write(current.tobytes());f.write(bytes([128])*(640*400//2))
    (OUT/'local-motion.json').write_text(json.dumps(dict(frames=12,static_through_frame=3,
        patch_size=[80,80],forward_motion_px=[-4,-3],
        cases={'bottom-right':[560,320],'top-left':[0,0]}),indent=2)+'\n')
