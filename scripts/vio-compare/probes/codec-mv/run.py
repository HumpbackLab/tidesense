#!/usr/bin/env python3
"""Serial full-sequence PC benchmark. Never skip hard frames."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[4]
WORK = ROOT / 'install/openvins-codec-mv'
DATASETS = {
    'tide': (ROOT / 'install/replay-20261012-084638/record', '.006'),
    'fpv': (ROOT / 'install/fisheye-benchmark/openvins/uzh-input', '.03'),
}

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--datasets', nargs='+', choices=list(DATASETS), default=list(DATASETS))
    p.add_argument('--modes', nargs='+', choices=['off', 'rect', 'median'], default=['off', 'rect', 'median'])
    p.add_argument('--caps', nargs='+', type=int, choices=[1000, 200], default=[1000, 200])
    p.add_argument('--repeat', type=int, default=1)
    args = p.parse_args()
    results = WORK / 'results'
    results.mkdir(exist_ok=True)
    for repeat in range(args.repeat):
        # Alternate ordering so a mode is not always last in repeated timing runs.
        modes = args.modes if repeat % 2 == 0 else list(reversed(args.modes))
        for name in args.datasets:
            record, ahead = DATASETS[name]
            for cap in args.caps:
                for mode in modes:
                    tag = f'{name}-r{cap}-{mode}-rep{repeat + 1}'
                    prefix = results / tag
                    # A previous completion marker must not validate a partial rerun.
                    Path(str(prefix) + '.json').unlink(missing_ok=True)
                    env = dict(os.environ, OV_CODEC_IMU_AHEAD=ahead, OV_CODEC_MV_MODE=mode,
                               OV_CODEC_MV_BIN=str(WORK / name / 'vectors.bin'),
                               OV_CODEC_MV_DETAIL=str(prefix) + '-detail.csv', OMP_NUM_THREADS='1',
                               OPENBLAS_NUM_THREADS='1')
                    cmd = [str(WORK / f'build/run_record.r{cap}'),
                           str(WORK / name / 'config/estimator_config.yaml'), str(record),
                           str(prefix) + '-poses.csv', '0']
                    start = time.monotonic()
                    with Path(str(prefix) + '.log').open('w') as log:
                        subprocess.run(cmd, env=env, stdout=log, stderr=log, check=True)
                    elapsed = time.monotonic() - start
                    Path(str(prefix) + '.json').write_text(json.dumps(dict(command=cmd,
                        environment={k: v for k, v in env.items() if k.startswith('OV_CODEC')},
                        wall_s=elapsed), indent=2) + '\n')
                    print(f'{tag}: wall_s={elapsed:.3f}', flush=True)
