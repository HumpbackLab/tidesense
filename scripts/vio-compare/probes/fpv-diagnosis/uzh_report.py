#!/usr/bin/env python3
"""Summarize UZH-FPV accuracy on explicitly shared GT timestamps."""
import argparse
import json
from pathlib import Path

import numpy as np

from replay_ab import rows
from uzh_download import atomic_json

METHODS = ('baseline', 'matched-imu', 'matched-imu-pixel-stable', 'openvins')


def position_ate(poses, timestamps, gt):
    origin = int(gt[0][0])
    tg = np.array([(int(row[0])-origin)*1e-9 for row in gt])
    gp = np.array([[float(v) for v in row[1:4]] for row in gt])
    t = np.array([(stamp-origin)*1e-9 for stamp in timestamps])
    x = np.array([poses[stamp] for stamp in timestamps])
    y = np.column_stack([np.interp(t,tg,gp[:,i]) for i in range(3)])
    if len(x) < 3 or not np.isfinite(x).all():
        return None
    u,_,vt = np.linalg.svd((x-x.mean(0)).T @ (y-y.mean(0)))
    sign = np.eye(3)
    sign[-1,-1] = np.linalg.det(vt.T@u.T)
    rotation = vt.T@sign@u.T
    error = np.linalg.norm((x-x.mean(0))@rotation.T+y.mean(0)-y,axis=1)
    return float(np.sqrt(np.mean(error**2)))


def summarize(root):
    state = json.loads((root/'suite.json').read_text())
    result = {'source': state['source'], 'binary_sha256': state['binary_sha256'],
              'library_sha256': state['library_sha256'],
              'openvins_binary_sha256': state.get('openvins_binary_sha256'),
              'noise_reference_sha256': state['noise_reference_sha256'], 'sequences': {}}
    for name,sequence in sorted(state['sequences'].items()):
        if sequence.get('status') not in ('complete','unsupported','prepare_failed'):
            continue
        entry = {k:v for k,v in sequence.items() if k != 'runs'}
        entry['runs'] = {key:dict(value) for key,value in sequence.get('runs',{}).items()}
        result['sequences'][name] = entry
        for method,run in entry['runs'].items():
            timing_file = root/name/method/'timing.csv'
            if timing_file.exists():
                timing = rows(timing_file)
                if timing and timing[0][0] == 'frame':
                    timing = timing[1:]
                run['timing_frames'] = len(timing)
                run['tracking_flag_frames'] = sum(int(row[3]) for row in timing)
        gt_file = root/name/'data/groundtruth.csv'
        if sequence.get('status') != 'complete' or not gt_file.exists():
            continue
        gt = rows(gt_file)
        start,end = int(gt[0][0]),int(gt[-1][0])
        expected = {int(row[0]) for row in rows(root/name/'data/stereo.csv') if start<=int(row[0])<=end}
        poses = {}
        for method,run in entry['runs'].items():
            if method == 'openvins' and run.get('initialized_pairs') == 0:
                run['status'] = 'not_initialized'
                continue
            if run['status'] != 'ok':
                continue
            file = root/name/method/'poses.csv'
            mapping = {int(row[0]):[float(v) for v in row[1:4]] for row in rows(file)}
            if not mapping or not np.isfinite(list(mapping.values())).all():
                entry['runs'][method]['status'] = 'invalid_output'
                continue
            poses[method] = mapping
        shared = set(expected)
        for mapping in poses.values():
            shared.intersection_update(mapping)
        timestamps = sorted(shared)
        entry.update(expected_gt_frames=len(expected), common_gt_frames=len(shared),
                     common_gt_coverage=len(shared)/len(expected) if expected else 0.)
        for method,mapping in poses.items():
            entry['runs'][method]['ate_common_m'] = position_ate(mapping,timestamps,gt)
            entry['runs'][method]['common_gt_frames'] = len(shared)
    public = [s for s in result['sequences'].values() if s.get('public_gt') and s.get('status')=='complete']
    joint = [s for s in public if s.get('common_gt_coverage') == 1. and
             all(s.get('runs', {}).get(m, {}).get('status') == 'ok' and
                 s['runs'][m].get('ate_common_m') is not None for m in METHODS)]
    result['joint_full_gt_comparison'] = {'sequences': len(joint),
        'sequence_names': [name for name,s in result['sequences'].items() if s in joint],
        'median_ate_m': {m: float(np.median([s['runs'][m]['ate_common_m'] for s in joint]))
                         if joint else None for m in METHODS}}
    result['aggregates'] = {}
    for method in METHODS:
        scores = [s['runs'][method]['ate_common_m'] for s in public if method in s.get('runs',{})
                  and s['runs'][method].get('status')=='ok' and s['runs'][method].get('ate_common_m') is not None
                  and s.get('common_gt_coverage')==1.]
        runs = [s['runs'][method] for s in result['sequences'].values() if method in s.get('runs',{})]
        result['aggregates'][method] = {'attempted_sequences':len(runs),
            'completed_sequences':sum(r.get('status')=='ok' for r in runs),
            'full_gt_scored_sequences':len(scores), 'median_ate_m':float(np.median(scores)) if scores else None}
    paired = [s for s in public if all(s.get('runs',{}).get(m,{}).get('status')=='ok'
              and s['runs'][m].get('ate_se3_rmse_m') is not None for m in ('matched-imu','matched-imu-pixel-stable'))]
    result['paired_backend_comparison'] = {'metric': 'full eligible GT ATE; independent of OpenVINS initialization', 'sequences':len(paired),
        'improved':sum(s['runs']['matched-imu-pixel-stable']['ate_se3_rmse_m'] < s['runs']['matched-imu']['ate_se3_rmse_m'] for s in paired),
        'worsened':sum(s['runs']['matched-imu-pixel-stable']['ate_se3_rmse_m'] > s['runs']['matched-imu']['ate_se3_rmse_m'] for s in paired)}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('results',type=Path)
    args=parser.parse_args()
    result=summarize(args.results)
    atomic_json(args.results/'comparison.json',result)
    for name,entry in result['sequences'].items():
        scores=' '.join(f'{method}={run.get("ate_common_m",run.get("status"))}' for method,run in entry.get('runs',{}).items())
        print(name,entry['status'],f'GT {entry.get("common_gt_frames","-")}/{entry.get("expected_gt_frames","-")}',scores)
    print(json.dumps(result['aggregates'],indent=2))


if __name__=='__main__':
    main()
