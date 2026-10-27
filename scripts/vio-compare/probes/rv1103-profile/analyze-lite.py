#!/usr/bin/env python3
"""Score VIO computation against a 50 ms budget, with trajectory validation."""
import argparse
import csv
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import numpy as np

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('work',type=Path)
args=parser.parse_args()
scripts=Path(__file__).resolve().parent
work=args.work.resolve();results=work/'results'
spec=importlib.util.spec_from_file_location('evaluate','/home/ncer/fpga_openvins/benchmarks/ov_schurvins_replay/evaluate_trajectory.py')
evaluator=importlib.util.module_from_spec(spec);spec.loader.exec_module(evaluator)
gt=scripts.parents[3]/'install/rv1103-openvins-20261025/dataset/mav0/state_groundtruth_estimate0/data.csv'
def stats(x):
 return dict(n=len(x),mean=float(np.mean(x)),p50=float(np.percentile(x,50)),p95=float(np.percentile(x,95)),
             p99=float(np.percentile(x,99)),maximum=float(np.max(x)))
trials={}
for status in sorted(results.glob('*.exit')):
 name=status.stem
 if status.read_text().strip()!='0':
  trials[name]={'exit_status':status.read_text().strip(),'complete':False};continue
 path=results/(name+'.json')
 b=json.loads(path.read_text())
 if b.get('samples')!=400:continue
 if b['initialization']!='native' or not b['outlier_gate']:
  raise ValueError(f'Unexpected algorithm options: {name}')
 poses=results/(name+'-poses.csv');v=np.loadtxt(poses,delimiter=',',comments='#')
 if v.shape[0]!=405 or np.any(v[:,12]<0):raise ValueError(f'Invalid state coverage/covariance: {name}')
 ate=evaluator.evaluate(poses,gt)
 summary=json.loads(subprocess.check_output([sys.executable,str(scripts/'analyze.py'),str(results/(name+'-detail.csv')),
                  '--stages',str(results/(name+'-stages.csv'))],text=True))
 rows=list(csv.DictReader((results/(name+'-detail.csv')).open()))
 selected=[r for r in rows if r['measured']=='1'];cost=[float(r['vio_ms']) for r in selected]
 first=int(rows[0]['timestamp_ns']);finish=0.;delays=[]
 for row in rows:
  arrival=(int(row['timestamp_ns'])-first)*1e-6
  finish=max(finish,arrival)+float(row['vio_ms'])
  if row['measured']=='1':delays.append(finish-arrival)
 backlog=max(0.,finish-((int(rows[-1]['timestamp_ns'])-first)*1e-6+50.))
 options={}
 meta=results/(name+'-variant.txt')
 if meta.exists():options=dict(line.split('=',1) for line in meta.read_text().splitlines() if '=' in line)
 trials[name]=dict(complete=True,benchmark=b,ate=ate,detail=summary,options=options,
   vio_ms=stats(cost),frames_exceeding_50_ms=sum(x>50 for x in cost),
   simulated_20hz_vio_only=dict(delay_ms=stats(delays),end_backlog_ms=backlog,
      frames_exceeding_50ms_delivery=sum(x>50 for x in delays)),
   covariance_min_diagonal=float(v[:,12].min()),covariance_max_asymmetry=float(v[:,13].max()),
   first_camera_timestamp_ns=int(v[0,0]),last_camera_timestamp_ns=int(v[-1,0]))
 (results/(name+'-ate.json')).write_text(json.dumps(ate,indent=2)+'\n')
 (results/(name+'-summary.json')).write_text(json.dumps(summary,indent=2)+'\n')
(work/'lite-results.json').write_text(json.dumps(trials,indent=2)+'\n')
for name,t in trials.items():
 if not t['complete']:print(name,'FAILED',t['exit_status']);continue
 print(f"{name}: VIO {t['vio_ms']['mean']:.2f}/{t['vio_ms']['p95']:.2f}/{t['vio_ms']['maximum']:.2f} ms mean/P95/max, "
       f">50ms={t['frames_exceeding_50_ms']}, ATE={t['ate']['position_ate_rmse_m']*100:.3f} cm")
