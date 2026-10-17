#!/usr/bin/env python3
"""Measure retry acceptance and future frontend track survival, plus replay quality."""
import csv
import json
import sys
from pathlib import Path
sys.path.insert(0, '/usr/lib/python3/dist-packages')
import numpy as np
from shallow_summary import ROOT, align

WORK = ROOT / 'install/openvins-codec-retry'
OLD = ROOT / 'install/openvins-codec-shallow'

def read(path):
    return list(csv.DictReader(path.open()))

def main():
    gt = np.loadtxt(ROOT / 'install/schurvins-gcc11-static/public-datasets/uzh-fpv/indoor_forward_6_replay/groundtruth.csv', delimiter=',')
    poses = {p.name.removesuffix('-poses.csv'): read(p) for p in (WORK/'results').glob('*-poses.csv')
             if p.with_name(p.name.removesuffix('-poses.csv')+'.json').exists()}
    fpv = [r for tag, r in poses.items() if tag.startswith('fpv')]
    common = sorted(set.intersection(*[{int(r['timestamp_ns']) for r in rows if r['initialized']=='1'} for rows in fpv]))
    common = [t for t in common if gt[0,0] <= t-16684572 <= gt[-1,0]]
    target = np.array(common, dtype=np.float64)-16684572
    truth = np.column_stack([np.interp(target,gt[:,0],gt[:,k]) for k in [1,2,3]])
    result = {}
    for tag, rows in sorted(poses.items()):
        name = tag.split('-')[0]
        prefix = WORK/'results'/tag
        initialized = [r for r in rows if r['initialized']=='1']
        stamps = {r['timestamp_ns'] for r in initialized[1:]}
        # Survival: continue to be retained on each of the next N actual camera frames.
        all_tracks = [{k:int(v) for k,v in r.items()} for r in read(Path(str(prefix)+'-tracks.csv'))]
        accepted = {(r['frame_index'],r['feature_id']) for r in all_tracks if r['retained']}
        max_frame = max(r['frame_index'] for r in all_tracks)
        tracks = [r for r in all_tracks if str(r['timestamp_ns']) in stamps]
        groups = {}
        for path,label in [(0,'direct_full'),(1,'shallow_accept'),(2,'retry')]:
            group = [r for r in tracks if r['path']==path]
            kept = [r for r in group if r['retained']]
            entry = dict(points=len(group), lk_valid=sum(r['lk_valid'] for r in group),
                         geometry_valid=sum(r['geometry_valid'] for r in group), retained=len(kept))
            for n in [1,3,5]:
                eligible = [r for r in kept if r['frame_index']+n<=max_frame]
                surviving = sum(all((r['frame_index']+j,r['feature_id']) in accepted for j in range(1,n+1)) for r in eligible)
                entry[f'next_{n}_frames'] = dict(eligible=len(eligible), surviving=surviving,
                                                fraction=surviving/len(eligible) if eligible else None)
            groups[label] = entry
        reasons = {}
        for reason,label in [(1,'lk_status'),(2,'nonfinite'),(3,'patch_residual'),(4,'refinement'),(5,'bounds')]:
            group = [r for r in tracks if r['path']==2 and r['shallow_failure']==reason]
            reasons[label] = dict(points=len(group), lk_valid=sum(r['lk_valid'] for r in group),
                                 geometry_valid=sum(r['geometry_valid'] for r in group),
                                 retained=sum(r['retained'] for r in group))
        p = np.array([[float(r[k]) for k in ['px','py','pz','qx','qy','qz','qw']] for r in initialized])
        entry = dict(frames=len(rows),initialized=len(initialized),finite=bool(np.isfinite(p).all()),
                     endpoint_m=float(np.linalg.norm(p[-1,:3]-p[0,:3])), paths=groups, retry_reasons=reasons)
        if tag.endswith('-on'):
            old = [r for r in read(OLD/f'host-results/{name}-shallow-rep1-poses.csv') if r['initialized']=='1']
            assert [r['timestamp_ns'] for r in old] == [r['timestamp_ns'] for r in initialized]
            op = np.array([[float(r[k]) for k in ['px','py','pz','qx','qy','qz','qw']] for r in old])
            assert len(p)==len(op)
            entry['prior_shallow_pose_max_abs_diff'] = float(np.max(np.abs(p-op)))
        if name=='fpv':
            lookup={int(r['timestamp_ns']):r for r in initialized}
            current=np.array([[float(lookup[t][k]) for k in ['px','py','pz']] for t in common])
            error=np.linalg.norm(align(current,truth)-truth,axis=1)
            entry['ate'] = dict(rmse_m=float(np.sqrt(np.mean(error**2))), samples=len(common),
                                duration_s=(common[-1]-common[0])*1e-9,alignment='SE3, fixed scale')
        native=[r for r in csv.reader(Path(str(prefix)+'-timing.csv').open()) if r and not r[0].startswith('#')]
        assert len(native) == len(initialized)
        detail=[r for r in read(Path(str(prefix)+'-detail.csv')) if r['timestamp_ns'] in stamps]
        entry['timing_ms'] = dict(frontend=float(np.mean([1000*float(r[1]) for r in native[1:]])),
                                 lk=float(np.mean([float(r['lk_ms']) for r in detail])),
                                 camera=float(np.mean([float(r['compute_ms']) for r in initialized[1:]])))
        result[tag]=entry
    (WORK/'retry-summary.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__': main()
