#!/usr/bin/env python3
"""Repeat selected completed runs with the same paths, preserving both outputs."""
import argparse
import json
from pathlib import Path
import shutil
import tempfile

from replay_ab import read_yaml
from uzh_download import atomic_json
from uzh_suite import prepare, run_native, VARIANTS


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--results',type=Path,required=True)
    parser.add_argument('--cache',type=Path,required=True)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--noise-reference',type=Path,required=True)
    parser.add_argument('--sequences',nargs='+',default=['outdoor_45_1','outdoor_forward_3','outdoor_forward_5'])
    parser.add_argument('--manifest',type=Path,default=Path(__file__).with_name('uzh-sequences.json'))
    args=parser.parse_args()
    for key in ('results','cache','binary','noise_reference'):
        setattr(args,key,getattr(args,key).resolve())
    catalogue={v['name']:v for v in json.loads(args.manifest.read_text())['sequences']}
    noise=read_yaml(args.noise_reference)['imu0']
    summary_file=args.results/'verification.json'
    summary=json.loads(summary_file.read_text()) if summary_file.exists() else {}
    for name in args.sequences:
        if name in summary:
            continue
        sequence=args.results/name
        summary[name]={}
        with tempfile.TemporaryDirectory(prefix='uzh-verify-',dir='/dev/shm') as temporary:
            prepare(args.cache,catalogue[name],sequence/'data',Path(temporary)/'images',noise)
            for variant in VARIANTS:
                original=sequence/variant
                first=sequence/'verification/first'/variant
                repeated=sequence/'verification/repeated'/variant
                shutil.copytree(original,first,dirs_exist_ok=True)
                first_result=json.loads((original/'result.json').read_text())
                try:
                    result=run_native(args.binary,variant,sequence/'data',original,300)
                    shutil.copytree(original,repeated,dirs_exist_ok=True)
                finally:
                    shutil.copytree(first,original,dirs_exist_ok=True)
                summary[name][variant]={'first':first_result,'repeated':result,
                    'byte_identical_poses':first_result.get('poses_sha256')==result.get('poses_sha256')}
                print(name,variant,'byte identical',summary[name][variant]['byte_identical_poses'],
                      'ATE',first_result.get('ate_se3_rmse_m'),result.get('ate_se3_rmse_m'),flush=True)
        atomic_json(summary_file,summary)


if __name__=='__main__':
    main()
