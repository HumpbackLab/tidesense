#!/usr/bin/env python3
"""Full-sequence quality and steady-state timing for gated MV observations."""
import csv
import json
from pathlib import Path
import sys
sys.path.insert(0,'/usr/lib/python3/dist-packages')
import numpy as np
from shallow_summary import ROOT,align,read,array,stats

WORK=ROOT/'install/openvins-codec-verify'

def main():
    poses={}
    for platform in ['host','a53']:
        for path in sorted((WORK/f'{platform}-results').glob('*-poses.csv')):
            tag=path.name.removesuffix('-poses.csv')
            if not path.with_name(tag+'.json').exists():continue
            poses[(platform,tag)]=read(path)
    gt=np.loadtxt(ROOT/'install/schurvins-gcc11-static/public-datasets/uzh-fpv/indoor_forward_6_replay/groundtruth.csv',delimiter=',')
    common=set.intersection(*[{int(r['timestamp_ns']) for r in rows if r['initialized']=='1'} for (_,tag),rows in poses.items() if tag.startswith('fpv')])
    common=sorted(t for t in common if gt[0,0]<=t-16684572<=gt[-1,0])
    target=np.array(common,dtype=np.float64)-16684572
    truth=np.column_stack([np.interp(target,gt[:,0],gt[:,k]) for k in [1,2,3]])
    result={};pose_arrays={}
    for (platform,tag),rows in poses.items():
        initialized=[r for r in rows if r['initialized']=='1']
        prefix=WORK/f'{platform}-results'/tag
        if not initialized:
            result[platform+'/'+tag]=dict(frames=len(rows),initialized=0);continue
        steady=initialized[1:];stamps={r['timestamp_ns'] for r in steady}
        details=[r for r in read(Path(str(prefix)+'-detail.csv')) if r['timestamp_ns'] in stamps]
        native=[r for r in csv.reader(Path(str(prefix)+'-timing.csv').open()) if r and not r[0].startswith('#')]
        assert len(native)==len(initialized)
        p=array(initialized,['px','py','pz','qx','qy','qz','qw'])
        pose_arrays[platform+'/'+tag]=p
        def total(k):return sum(float(r[k]) for r in details)
        points=total('points');accepted=total('verified_points')
        e=dict(frames=len(rows),initialized=len(initialized),finite=bool(np.isfinite(p).all()),
               endpoint_m=float(np.linalg.norm(p[-1,:3]-p[0,:3])),
               max_displacement_m=float(np.linalg.norm(p[:,:3]-p[0,:3],axis=1).max()),
               frontend_ms=stats([1000*float(r[1]) for r in native[1:]]),
               camera_ms=stats([float(r['compute_ms']) for r in steady]),
               cache_ms=stats([float(r['mv_prepare_ms']) for r in steady]),
               tracking=dict(calls=len(details),points=points,
                 seeded_fraction=total('seeded')/points,skip_fraction=accepted/points,
                 full_fraction=total('full_points')/points,
                 verify_ms=total('verify_ms')/len(steady),lk_full_ms=total('full_ms')/len(steady),
                 match_ms=total('lk_ms')/len(steady),ransac_ms=total('ransac_ms')/len(steady),
                 verified_geometry_fraction=total('verified_geometry_valid')/accepted if accepted else None,
                 mean_geometry_points=total('geometry_valid')/len(details)))
        if tag.startswith('fpv'):
            lookup={int(r['timestamp_ns']):r for r in initialized}
            pp=array([lookup[t] for t in common],['px','py','pz'])
            error=np.linalg.norm(align(pp,truth)-truth,axis=1)
            e['ate']=dict(rmse_m=float(np.sqrt(np.mean(error**2))),p95_m=float(np.percentile(error,95)),
                          samples=len(common),duration_s=(common[-1]-common[0])*1e-9,alignment='SE3 fixed scale')
        if '-base-' in tag:
            dataset=tag.split('-')[0]
            old=read(ROOT/f'install/openvins-codec-shallow/{platform}-results/{dataset}-base-rep1-poses.csv')
            old=[r for r in old if r['initialized']=='1']
            assert [r['timestamp_ns'] for r in old]==[r['timestamp_ns'] for r in initialized]
            e['baseline_pose_max_abs_diff']=float(np.max(np.abs(p-array(old,['px','py','pz','qx','qy','qz','qw']))))
        result[platform+'/'+tag]=e
    (WORK/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
    aggregate={}
    for group in sorted({tag.rsplit('-rep',1)[0] for tag in result}):
        runs={tag:r for tag,r in result.items() if tag.rsplit('-rep',1)[0]==group and r['initialized']}
        if not runs:continue
        def mean(get):return float(np.mean([get(r) for r in runs.values()]))
        arrays=[pose_arrays[tag] for tag in runs]
        same_shape=all(p.shape==arrays[0].shape for p in arrays)
        aggregate[group]=dict(repeats=len(runs),
          frontend_ms=mean(lambda r:r['frontend_ms']['mean']),
          cache_ms=mean(lambda r:r['cache_ms']['mean']),
          effective_frontend_ms=mean(lambda r:r['frontend_ms']['mean']+r['cache_ms']['mean']),
          camera_ms=mean(lambda r:r['camera_ms']['mean']),
          match_ms=mean(lambda r:r['tracking']['match_ms']),
          full_lk_ms=mean(lambda r:r['tracking']['lk_full_ms']),
          verification_ms=mean(lambda r:r['tracking']['verify_ms']),
          ransac_ms=mean(lambda r:r['tracking']['ransac_ms']),
          skip_fraction=mean(lambda r:r['tracking']['skip_fraction']),
          endpoint_m=mean(lambda r:r['endpoint_m']),
          ate_mean_m=mean(lambda r:r['ate']['rmse_m']) if all('ate' in r for r in runs.values()) else None,
          repeat_pose_max_abs_diff=max(float(np.max(np.abs(p-arrays[0]))) for p in arrays) if same_shape else None)
    (WORK/'aggregate.json').write_text(json.dumps(aggregate,indent=2)+'\n')
    print('run,frames,init,ATE_m,endpoint_m,skip_pct,verify_ms,full_LK_ms,match_ms,front_ms,front_cache_ms,camera_ms')
    for tag,r in result.items():
        if not r['initialized']:print(tag,'NO INITIALIZATION');continue
        t=r['tracking']
        print(f"{tag},{r['frames']},{r['initialized']},{r.get('ate',{}).get('rmse_m',float('nan')):.4f},{r['endpoint_m']:.3f},"
              f"{100*t['skip_fraction']:.1f},{t['verify_ms']:.3f},{t['lk_full_ms']:.3f},{t['match_ms']:.3f},"
              f"{r['frontend_ms']['mean']:.3f},{r['frontend_ms']['mean']+r['cache_ms']['mean']:.3f},{r['camera_ms']['mean']:.3f}")

if __name__=='__main__':main()
