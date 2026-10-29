#!/usr/bin/env python3
"""Summarize CPU/synchronous/pipelined replay without conflating SD I/O and VIO."""
import argparse
import csv
import json
from pathlib import Path
import re
import numpy as np

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('results',type=Path)
p.add_argument('--gt',type=Path)
p.add_argument('--shift-ns',type=int,default=-16684572)
a=p.parse_args()
names=['cpu','sync','pipeline']
tables={n:list(csv.DictReader((a.results/(n+'.csv')).open()))for n in names}
columns=['initialized','px','py','pz','qx','qy','qz','qw']
report={'variants':{}}
for name,rows in tables.items():
    ready=[r for r in rows if r['initialized']=='1']
    if not ready:raise RuntimeError(name+' did not initialize')
    times=np.array([float(r['compute_ms'])for r in rows])
    init_times=np.array([float(r['compute_ms'])for r in ready])
    values=np.array([[float(r[k])for k in columns[1:]]for r in ready])
    if not np.isfinite(values).all():raise RuntimeError(name+' has non-finite poses')
    log=(a.results/(name+'.log')).read_text(errors='replace')
    done=re.findall(r'DONE frames=(\d+).*?wall_s=([\d.]+)',log)
    if not done or int(done[-1][0])!=len(rows):raise RuntimeError(name+' did not complete')
    report['variants'][name]=dict(frames=len(rows),initialized=len(ready),
        compute_mean_ms=float(times.mean()),compute_initialized_ms=float(init_times.mean()),
        compute_p95_ms=float(np.percentile(init_times,95)),compute_fps=float(1000/times.mean()),
        wall_s=float(done[-1][1]),wall_fps=len(rows)/float(done[-1][1]),
        pose_identical_to_cpu=len(rows)==len(tables['cpu']) and all(all(x[k]==y[k]for k in columns)for x,y in zip(rows,tables['cpu'])),
        **{k:float(np.mean([float(r[k])for r in ready]))for k in
           ['tpu_hits','tpu_points','tpu_prepare_ms','tpu_remote_ms','tpu_wait_cpu_ms','tpu_overlap_ms']})
    report['variants'][name]['last_position_m']=values[-1,:3].tolist()
report['pipeline_sync_identical']=len(tables['sync'])==len(tables['pipeline']) and all(
    all(x[k]==y[k]for k in columns)for x,y in zip(tables['sync'],tables['pipeline']))
poses={n:{int(r['timestamp_ns']):np.array([float(r[k])for k in ['px','py','pz']])
          for r in rows if r['initialized']=='1'}for n,rows in tables.items()}
common=sorted(set.intersection(*(set(x)for x in poses.values())))
if a.gt:
    gt=np.loadtxt(a.gt,delimiter=',',comments='#')
    common=[t for t in common if gt[0,0]<=t+a.shift_ns<=gt[-1,0]]
    truth=np.column_stack([np.interp(np.array(common)+a.shift_ns,gt[:,0],gt[:,i])for i in [1,2,3]])
    report['common_gt_frames']=len(common)
    for name in names:
        estimate=np.array([poses[name][t]for t in common]);x=estimate.mean(0);y=truth.mean(0)
        u,_,vt=np.linalg.svd((estimate-x).T@(truth-y));d=np.eye(3);d[-1,-1]=np.linalg.det(u@vt)
        error=np.linalg.norm((estimate-x)@(u@d@vt)+y-truth,axis=1)
        report['variants'][name]['ate_rmse_m']=float(np.sqrt(np.mean(error**2)))
(a.results/'comparison.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
