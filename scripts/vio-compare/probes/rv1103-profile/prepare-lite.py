#!/usr/bin/env python3
"""Create single-variable simplification configurations for the FP32 RKIVE replay."""
import argparse
import json
from pathlib import Path
import re
import shutil

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('output',type=Path)
parser.add_argument('--base',type=Path,default=Path('/home/ncer/sophpi/install/rv1103-backend-20261025'))
args=parser.parse_args()
work=args.output.resolve()
work.mkdir(parents=True,exist_ok=True)
src=work/'src/benchmarks'
if src.exists():raise SystemExit(f'Refusing to overwrite {src}')
shutil.copytree(args.base/'src/benchmarks',src)
base=args.base/'build/stage/share/openvins/euroc_mav'
variants={
 'baseline':{},
 'nohist':{'histogram_method':'"NONE"'},
 'n150':{'histogram_method':'"NONE"','num_pts':'150'},
 'n120':{'histogram_method':'"NONE"','num_pts':'120'},
 'n100':{'histogram_method':'"NONE"','num_pts':'100'},
 'n150-cap10':{'histogram_method':'"NONE"','num_pts':'150','max_msckf_in_update':'10'},
 'n120-cap10':{'histogram_method':'"NONE"','num_pts':'120','max_msckf_in_update':'10'},
 'n120-cap10-refine2':{'histogram_method':'"NONE"','num_pts':'120','max_msckf_in_update':'10','fi_max_runs':'2'},
 'n120-cap8-refine2':{'histogram_method':'"NONE"','num_pts':'120','max_msckf_in_update':'8','fi_max_runs':'2'},
 'n120-cap20':{'histogram_method':'"NONE"','num_pts':'120','max_msckf_in_update':'20'},
 'n120-calibfixed':{'histogram_method':'"NONE"','num_pts':'120','calib_cam_extrinsics':'false','calib_cam_intrinsics':'false'},
 'half120':{'histogram_method':'"NONE"','num_pts':'120','downsample_cameras':'true','init_max_disparity':'5.0'},
}
for name,settings in variants.items():
 target=work/'configs'/name
 shutil.copytree(base,target)
 p=target/'estimator_config.yaml';text=p.read_text()
 for key,value in settings.items():
  text,count=re.subn(r'^'+key+r':[^#\n]*',key+': '+value+' ',text,flags=re.M)
  if count==0 and key.startswith('fi_'): text+='\n'+key+': '+value+'\n'
  else: assert count==1,key
 p.write_text(text)
(work/'variants.json').write_text(json.dumps(variants,indent=2)+'\n')
print(work)
