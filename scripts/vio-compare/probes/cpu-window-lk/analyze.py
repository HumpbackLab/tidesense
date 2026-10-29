#!/usr/bin/env python3
"""Summarize per-call LK cost and OpenCV/TPU point differences; no VIO accuracy claim."""
import argparse,csv,json,math
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser(description=__doc__);p.add_argument('results',type=Path);a=p.parse_args()
rows=list(csv.DictReader((a.results/'timings.csv').open()))
log=(a.results/'run.log').read_text(errors='replace')
assert 'CPU_EXACT=1' in log and 'DONE cases=' in log,'Incomplete benchmark'
report={'cases':len({tuple(r[k] for k in ['frame','camera','gap','group','count'])for r in rows}),'measured_calls':len(rows),'cpu_matches_full_gradient_opencv':True,'counts':{}}
def stats(v):
 v=np.asarray(v,dtype=float)
 return dict(mean=float(v.mean()),median=float(np.median(v)),p95=float(np.percentile(v,95)),maximum=float(v.max()))
for count in [16,32]:
 out={};subset=[r for r in rows if int(r['count'])==count]
 for mode in sorted({r['mode'] for r in subset}):
  rr=[r for r in subset if r['mode']==mode]
  m={k:stats([float(r[k])for r in rr])for k in ['wall_ms','cpu_ms','gradient_ms','lk_ms','sync_ms','device_ms']}
  m['calls']=len(rr);m['valid_fraction']=float(np.mean([int(r['valid'])/count for r in rr]))
  # Worst individual sample includes scheduler noise; also report the slowest
  # case median across the 15 repeats, a more useful batch-sizing reference.
  groups={}
  for r in rr:groups.setdefault(tuple(r[k]for k in ['frame','camera','gap','group']),[]).append(float(r['wall_ms']))
  key=max(groups,key=lambda k:np.median(groups[k]))
  m['worst_case_median']={'frame':int(key[0]),'camera':int(key[1]),'gap':int(key[2]),'group':key[3],'wall_ms':float(np.median(groups[key]))}
  m['case_median_wall_ms']=stats([np.median(v)for v in groups.values()])
  m['by_gap']={str(gap):stats([float(r['wall_ms'])for r in rr if int(r['gap'])==gap])for gap in [1,2]}
  m['by_group']={g:{k:stats([float(r[k])for r in rr if r['group']==g])for k in ['wall_ms','gradient_ms','lk_ms','sync_ms']}for g in ['head','tail']}
  out[mode]=m
 report['counts'][str(count)]=out
point_file=a.results/'points.csv'
if point_file.exists():
 points=list(csv.DictReader(point_file.open()))
 if points:
  point_report={}
  for count in [16,32]:
   pp=[r for r in points if int(r['count'])==count];common=[r for r in pp if r['cpu_valid']=='1' and r['tpu_valid']=='1']
   errors=[math.hypot(float(r['cpu_x'])-float(r['tpu_x']),float(r['cpu_y'])-float(r['tpu_y']))for r in common]
   point_report[str(count)]={'points':len(pp),'status_agree':sum(r['cpu_valid']==r['tpu_valid']for r in pp),'common_valid':len(common),'coordinate_error_px':stats(errors),'over_0_1px':sum(v>.1 for v in errors),'over_1px':sum(v>1 for v in errors)}
  report['cpu_vs_tpu']=point_report
  outliers=[]
  for r in points:
   if r['cpu_valid']=='1' and r['tpu_valid']=='1':
    distance=math.hypot(float(r['cpu_x'])-float(r['tpu_x']),float(r['cpu_y'])-float(r['tpu_y']))
    if distance>.1:outliers.append(dict(**r,error_px=distance))
  outliers.sort(key=lambda r:r['error_px'],reverse=True)
  (a.results/'outliers.json').write_text(json.dumps(outliers,indent=2)+'\n')
(a.results/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
