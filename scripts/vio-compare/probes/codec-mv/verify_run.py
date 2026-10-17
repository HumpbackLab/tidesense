#!/usr/bin/env python3
"""Complete original-image replays for direct H265 MV observations with patch gates."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
from shallow_run import ROOT,RECORDS

WORK=ROOT/'install/openvins-codec-verify'
CACHE=ROOT/'install/openvins-codec-shallow'
CASES={
    'base':dict(OV_CODEC_MV_MODE='off',OV_CODEC_MV_LK='full'),
    'strict':dict(OV_CODEC_MV_MODE='rect',OV_CODEC_MV_LK='verify',
                  OV_CODEC_MV_VERIFY_MAE='4',OV_CODEC_MV_VERIFY_HAMMING='3',OV_CODEC_MV_VERIFY_REFRESH='5'),
    'balanced':dict(OV_CODEC_MV_MODE='rect',OV_CODEC_MV_LK='verify',
                    OV_CODEC_MV_VERIFY_MAE='8',OV_CODEC_MV_VERIFY_HAMMING='6',OV_CODEC_MV_VERIFY_REFRESH='5'),
}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--datasets',nargs='+',choices=list(RECORDS),default=list(RECORDS))
    p.add_argument('--cases',nargs='+',choices=list(CASES),default=list(CASES))
    p.add_argument('--repeat',type=int,default=1)
    a=p.parse_args()
    out=WORK/'host-results';out.mkdir(parents=True,exist_ok=True)
    for rep in range(1,a.repeat+1):
        for name in a.datasets:
            for case in a.cases:
                prefix=out/f'{name}-{case}-rep{rep}'
                Path(str(prefix)+'.json').unlink(missing_ok=True)
                env=dict(os.environ,**CASES[case],
                    OV_CODEC_IMU_AHEAD='.006' if name=='tide' else '.03',
                    OV_CODEC_MV_BIN=str(CACHE/name/'vectors.bin'),
                    OV_CODEC_MV_DETAIL=str(prefix)+'-detail.csv',
                    OMP_NUM_THREADS='1',OPENBLAS_NUM_THREADS='1')
                command=[str(WORK/'host/run_record.r1000'),
                         str(CACHE/f'host-config-{name}/estimator_config.yaml'),
                         str(RECORDS[name]),str(prefix)+'-poses.csv','0']
                start=time.monotonic()
                with Path(str(prefix)+'.log').open('w') as log:
                    subprocess.run(command,env=env,stdout=log,stderr=log,check=True)
                shutil.copy2('/tmp/codec-shallow-timing.csv',str(prefix)+'-timing.csv')
                Path(str(prefix)+'.json').write_text(json.dumps(dict(command=command,
                    environment={k:v for k,v in env.items() if k.startswith('OV_CODEC')},
                    wall_s=time.monotonic()-start),indent=2)+'\n')
                print(prefix.name,f'{time.monotonic()-start:.2f}s',flush=True)
