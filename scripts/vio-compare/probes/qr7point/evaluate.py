#!/usr/bin/env python3
"""Compare QR7 FPV trajectories on common GT samples without fitting scale."""
import argparse,csv,json,re
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser(description=__doc__);p.add_argument('results',type=Path);p.add_argument('--modes',nargs='+',default=['off','force','qr']);p.add_argument('--reference',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[4]
gt=np.loadtxt(root/'install/schurvins-gcc11-static/public-datasets/uzh-fpv/indoor_forward_6_replay/groundtruth.csv',delimiter=',',comments='#')
def read(f):
 with f.open()as stream:return list(csv.DictReader(stream))
rows={m:read(a.results/(m+'.csv')) for m in a.modes}
if a.reference:rows['reference']=read(a.reference)
keys=['timestamp_ns','initialized','px','py','pz','qx','qy','qz','qw'];base=rows.get('reference',rows[a.modes[0]])
points={m:{int(r['timestamp_ns'])-16684572:r for r in rr if r['initialized']=='1'} for m,rr in rows.items()}
times=sorted(set.intersection(*(set(v) for v in points.values())));times=np.array([t for t in times if gt[0,0]<=t<=gt[-1,0]],dtype=np.int64)
assert len(times)>100,'Insufficient common ground truth'
truth=np.column_stack([np.interp(times,gt[:,0],gt[:,i]) for i in [1,2,3]])
report=dict(common_gt_frames=len(times),shift_ns=-16684572,alignment='SE3, no scale',variants={})
for m,rr in rows.items():
 if m!='reference':
  log=(a.results/(m+'.log')).read_text(errors='replace');assert f'DONE frames={len(rr)} 'in log,m+' incomplete'
 x=np.array([[float(points[m][int(t)][k]) for k in ['px','py','pz']]for t in times]);xm=x.mean(0);ym=truth.mean(0);u,_,vt=np.linalg.svd((x-xm).T@(truth-ym));d=np.eye(3);d[-1,-1]=np.linalg.det(u@vt);error=np.linalg.norm((x-xm)@(u@d@vt)+ym-truth,axis=1)
 ready=[r for r in rr if r['initialized']=='1'];finite=all(np.isfinite(float(r[k]))for r in ready for k in ['px','py','pz','qx','qy','qz','qw'])
 report['variants'][m]=dict(frames=len(rr),initialized_frames=len(ready),finite=finite,poses_identical_to_reference=len(rr)<=len(base) and all(x[k]==y[k]for x,y in zip(rr,base)for k in keys),ate_rmse_m=float(np.sqrt(np.mean(error**2))),ate_p95_m=float(np.percentile(error,95)),ate_max_m=float(error.max()))
 if m!='reference':
  report['variants'][m]['qr_stats']=re.findall(r'QR7 .*',(a.results/(m+'.log')).read_text())
(a.results/'accuracy.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
