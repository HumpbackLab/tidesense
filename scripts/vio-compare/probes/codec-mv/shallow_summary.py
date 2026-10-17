#!/usr/bin/env python3
"""Compare full-sequence quality and frontend timing, with real hardware MVs."""
import csv
import json
from pathlib import Path
import re
import statistics
import sys
sys.path.insert(0, '/usr/lib/python3/dist-packages')
import numpy as np

ROOT = Path(__file__).resolve().parents[4]
WORK = ROOT / 'install/openvins-codec-shallow'

def read(path):
    return list(csv.DictReader(path.open()))

def array(rows, keys):
    return np.array([[float(r[k]) for k in keys] for r in rows])

def stats(a):
    return dict(mean=float(np.mean(a)), p95=float(np.percentile(a,95)), max=float(np.max(a)))

def align(p, q):
    pc, qc = p.mean(0), q.mean(0)
    u, _, vt = np.linalg.svd((p-pc).T @ (q-qc))
    d = np.eye(3); d[2,2] = np.linalg.det(u @ vt)
    return (p-pc) @ (u @ d @ vt) + qc

def main():
    poses = {}
    for platform in ['host','a53']:
        for file in sorted((WORK / f'{platform}-results').glob('*-poses.csv')):
            tag = file.name.removesuffix('-poses.csv')
            if not file.with_name(tag+'.json').exists(): continue
            poses[(platform,tag)] = read(file)
    gt = np.loadtxt(ROOT / 'install/schurvins-gcc11-static/public-datasets/uzh-fpv/indoor_forward_6_replay/groundtruth.csv', delimiter=',')
    fpv = [rows for (_,tag),rows in poses.items() if tag.startswith('fpv-')]
    common = set.intersection(*[{int(r['timestamp_ns']) for r in rows if r['initialized']=='1'} for rows in fpv]) if fpv else set()
    common = sorted(t for t in common if gt[0,0] <= t-16684572 <= gt[-1,0])
    target = np.array(common,dtype=np.float64)-16684572
    truth = np.column_stack([np.interp(target,gt[:,0],gt[:,k]) for k in [1,2,3]])
    result = {}
    for (platform,tag),rows in poses.items():
        name,case,rep = tag.split('-')
        initialized = [r for r in rows if r['initialized']=='1']
        if not initialized:
            result[f'{platform}/{tag}'] = dict(frames=len(rows), initialized=0)
            continue
        steady = initialized[1:]
        stamps = {r['timestamp_ns'] for r in steady}
        p = array(initialized,['px','py','pz','qx','qy','qz','qw'])
        prefix = WORK / f'{platform}-results/{tag}'
        details = [r for r in read(Path(str(prefix)+'-detail.csv')) if r['timestamp_ns'] in stamps]
        native = [r for r in csv.reader(Path(str(prefix)+'-timing.csv').open()) if r and not r[0].startswith('#')]
        front = [1000*float(r[1]) for r in native[1:]]
        entry = dict(frames=len(rows), initialized=len(initialized), finite=bool(np.isfinite(p).all()),
                     endpoint_m=float(np.linalg.norm(p[-1,:3]-p[0,:3])),
                     max_displacement_m=float(np.linalg.norm(p[:,:3]-p[0,:3],axis=1).max()),
                     camera_ms=stats([float(r['compute_ms']) for r in steady]),
                     frontend_ms=stats(front), timing_rows=len(native),
                     cache_prepare_ms=stats([float(r['mv_prepare_ms']) for r in steady]))
        def total(field): return sum(float(r[field]) for r in details)
        points, shallow = total('points'), total('shallow_points')
        entry['tracking'] = dict(calls=len(details), mean_points=points/len(details),
            seeded_fraction=total('seeded')/points, shallow_fraction=shallow/points,
            full_fraction=total('full_points')/points,
            retry_fraction_of_shallow=total('full_retries')/shallow if shallow else 0,
            lk_ms_per_steady_frame=total('lk_ms')/len(steady),
            ransac_ms_per_steady_frame=total('ransac_ms')/len(steady),
            query_ms_per_steady_frame=total('seed_ms')/len(steady),
            mean_geometry_points=total('geometry_valid')/len(details),
            geometry_below20_calls=sum(int(r['geometry_valid'])<20 for r in details))
        quality = [r for r in details if int(r['seeded_geometry_valid'])]
        if quality:
            entry['proposal_diagnostics'] = dict(
                seed_px=statistics.median(float(r['seed_error_median_px']) for r in quality),
                zero_px=statistics.median(float(r['zero_error_median_px']) for r in quality))
        if name=='fpv':
            lookup={int(r['timestamp_ns']):r for r in initialized}
            current=array([lookup[t] for t in common],['px','py','pz'])
            error=np.linalg.norm(align(current,truth)-truth,axis=1)
            entry['ate']=dict(rmse_m=float(np.sqrt(np.mean(error**2))), p95_m=float(np.percentile(error,95)),
                              max_m=float(error.max()), samples=len(common), duration_s=(common[-1]-common[0])*1e-9,
                              alignment='SE3, fixed scale')
        if case=='base' and platform=='host':
            old=read(ROOT / f'install/openvins-mono-front/host-results/{name}-base-poses.csv')
            old=[r for r in old if r['initialized']=='1']
            entry['historical_pose_max_abs_diff']=float(np.max(np.abs(p-array(old,['px','py','pz','qx','qy','qz','qw']))))
        result[f'{platform}/{tag}']=entry
    (WORK/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
    groups = {}
    for key in sorted({tag.rsplit('-',1)[0] for tag in result}):
        runs = [r for tag,r in result.items() if tag.rsplit('-',1)[0] == key]
        if any(not r['initialized'] for r in runs): continue
        def median(path):
            values=[]
            for run in runs:
                value=run
                for part in path.split('.'): value=value[part]
                values.append(value)
            return float(np.median(values))
        groups[key]=dict(repeats=len(runs), frontend_ms=median('frontend_ms.mean'),
            effective_frontend_ms=float(np.median([r['frontend_ms']['mean']+r['cache_prepare_ms']['mean'] for r in runs])),
            cache_prepare_ms=median('cache_prepare_ms.mean'), camera_ms=median('camera_ms.mean'),
            lk_ms=median('tracking.lk_ms_per_steady_frame'),
            ransac_ms=median('tracking.ransac_ms_per_steady_frame'),
            seeded_fraction=median('tracking.seeded_fraction'),
            retry_fraction_of_shallow=median('tracking.retry_fraction_of_shallow'),
            full_fraction=median('tracking.full_fraction'),
            endpoint_m=runs[0]['endpoint_m'], ate=runs[0].get('ate'))
    (WORK/'aggregate.json').write_text(json.dumps(groups,indent=2)+'\n')
    print('run,frames,init,endpoint_m,ATE_m,frontend_ms,LK_ms,camera_ms,seed_pct,retry_pct,full_pct')
    for tag,r in result.items():
        if not r['initialized']: print(tag,'FAILED INITIALIZATION'); continue
        t=r['tracking']
        print(f"{tag},{r['frames']},{r['initialized']},{r['endpoint_m']:.3f},{r.get('ate',{}).get('rmse_m',float('nan')):.4f},"
              f"{r['frontend_ms']['mean']:.3f},{t['lk_ms_per_steady_frame']:.3f},{r['camera_ms']['mean']:.3f},"
              f"{100*t['seeded_fraction']:.1f},{100*t['retry_fraction_of_shallow']:.1f},{100*t['full_fraction']:.1f}")
    hardware={}
    for name in ['fpv','tide']:
        log=(WORK/name/'encode.log')
        if not log.exists(): continue
        text=log.read_text()
        encoded=re.findall(r'^ENCODED .*send_to_stream_ms=([\d.]+)',text,re.M)
        acquired=re.findall(r'^MV_CACHE .*P=1.*acquire_parse_ms=([\d.]+)',text,re.M)
        hardware[name]=dict(frames=len(encoded), send_to_stream_ms=stats(list(map(float,encoded[1:]))),
                            acquire_parse_ms=stats(list(map(float,acquired))))
    (WORK/'hardware-timing.json').write_text(json.dumps(hardware,indent=2)+'\n')

if __name__=='__main__': main()
