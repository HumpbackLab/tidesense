#!/usr/bin/env python3
"""Retry usefulness ablation, on unchanged full recordings and hardware MV caches."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
from shallow_run import ROOT, RECORDS

WORK = ROOT / 'install/openvins-codec-retry'
PREVIOUS = ROOT / 'install/openvins-codec-shallow'

def main():
    out = WORK / 'results'
    out.mkdir(parents=True, exist_ok=True)
    for name, record in RECORDS.items():
        for retry in ['on', 'off']:
            prefix = out / f'{name}-retry-{retry}'
            env = dict(os.environ, OV_CODEC_IMU_AHEAD='.006' if name == 'tide' else '.03',
                       OV_CODEC_MV_MODE='rect', OV_CODEC_MV_LK='shallow', OV_CODEC_MV_RETRY=retry,
                       OV_CODEC_MV_BIN=str(PREVIOUS / name / 'vectors.bin'),
                       OV_CODEC_MV_DETAIL=str(prefix)+'-detail.csv',
                       OV_CODEC_MV_TRACKS=str(prefix)+'-tracks.csv',
                       OMP_NUM_THREADS='1', OPENBLAS_NUM_THREADS='1')
            command = [str(WORK / 'host/run_record.r1000'),
                       str(PREVIOUS / f'host-config-{name}/estimator_config.yaml'),
                       str(record), str(prefix)+'-poses.csv', '0']
            Path(str(prefix)+'.json').unlink(missing_ok=True)
            start = time.monotonic()
            with Path(str(prefix)+'.log').open('w') as log:
                subprocess.run(command, env=env, stdout=log, stderr=log, check=True)
            shutil.copy2('/tmp/codec-shallow-timing.csv', str(prefix)+'-timing.csv')
            Path(str(prefix)+'.json').write_text(json.dumps(dict(command=command,
                environment={k:v for k,v in env.items() if k.startswith('OV_CODEC')},
                wall_s=time.monotonic()-start), indent=2)+'\n')
            print(prefix.name, f'{time.monotonic()-start:.2f}s', flush=True)

if __name__ == '__main__': main()
