#!/usr/bin/env python3
"""PC accuracy experiment using actual board-encoded MV caches."""
import argparse
import csv
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[4]
WORK = ROOT / 'install/openvins-codec-shallow'
CASES = {'base': ('off', 'full'), 'mvfull': ('rect', 'full'), 'shallow': ('rect', 'shallow')}
RECORDS = {'tide': ROOT / 'install/replay-20261012-084638/record',
           'fpv': ROOT / 'install/fisheye-benchmark/openvins/uzh-input'}

def configs(platform):
    for name in RECORDS:
        dest = WORK / f'{platform}-config-{name}'
        shutil.copytree(ROOT / f'install/openvins-codec-mv/{name}/config', dest, dirs_exist_ok=True)
        path = dest / 'estimator_config.yaml'
        text = path.read_text().replace('record_timing_information: false', 'record_timing_information: true')
        text = re.sub(r'^record_timing_filepath:.*$', 'record_timing_filepath: "/tmp/codec-shallow-timing.csv"', text, flags=re.M)
        path.write_text(text)

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--datasets', nargs='+', choices=list(RECORDS), default=list(RECORDS))
    p.add_argument('--cases', nargs='+', choices=list(CASES), default=list(CASES))
    p.add_argument('--repeat', type=int, default=1)
    p.add_argument('--prepare-a53', action='store_true')
    p.add_argument('--prepare-manifests', action='store_true')
    args = p.parse_args()
    if args.prepare_manifests:
        for name, record in RECORDS.items():
            out = WORK / name
            out.mkdir(parents=True, exist_ok=True)
            board = '/mnt/data/openvins-codec-shallow/tide-record' if name == 'tide' else '/mnt/data/public-vio-benchmark/uzh-fpv-indoor6'
            rows = [r for r in csv.reader((record/'stereo.csv').open()) if r and not r[0].startswith('#')]
            (out/'frames.txt').write_text(''.join(f'{r[0]} {board}/{r[1]}\n' for r in rows))
            (out/'left-files.txt').write_text(''.join(r[1]+'\n' for r in rows))
        raise SystemExit(0)
    if args.prepare_a53:
        configs('a53')
        raise SystemExit(0)
    configs('host')
    out = WORK / 'host-results'
    out.mkdir(exist_ok=True)
    for repeat in range(args.repeat):
        for name in args.datasets:
            for case in args.cases if repeat % 2 == 0 else list(reversed(args.cases)):
                mode, lk = CASES[case]
                prefix = out / f'{name}-{case}-rep{repeat+1}'
                Path(str(prefix)+'.json').unlink(missing_ok=True)
                env = dict(os.environ, OV_CODEC_IMU_AHEAD='.006' if name == 'tide' else '.03',
                           OV_CODEC_MV_MODE=mode, OV_CODEC_MV_LK=lk,
                           OV_CODEC_MV_BIN=str(WORK / name / 'vectors.bin'),
                           OV_CODEC_MV_DETAIL=str(prefix)+'-detail.csv',
                           OMP_NUM_THREADS='1', OPENBLAS_NUM_THREADS='1')
                command = [str(WORK / 'host/run_record.r1000'),
                           str(WORK / f'host-config-{name}/estimator_config.yaml'),
                           str(RECORDS[name]), str(prefix)+'-poses.csv', '0']
                start = time.monotonic()
                with Path(str(prefix)+'.log').open('w') as log:
                    subprocess.run(command, env=env, stdout=log, stderr=log, check=True)
                shutil.copy2('/tmp/codec-shallow-timing.csv', str(prefix)+'-timing.csv')
                Path(str(prefix)+'.json').write_text(json.dumps(dict(command=command,
                    environment={k:v for k,v in env.items() if k.startswith('OV_CODEC')},
                    wall_s=time.monotonic()-start), indent=2)+'\n')
                print(prefix.name, f'{time.monotonic()-start:.2f}s', flush=True)
