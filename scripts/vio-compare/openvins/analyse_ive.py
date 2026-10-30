#!/usr/bin/env python3
"""Measure the complete IVE frontend experiment on one preloaded FPV window."""
import argparse
import csv
import json
import re
from pathlib import Path
import numpy as np

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('results',type=Path)
p.add_argument('--modes',nargs='+',default=['off','hist','st','both'])
p.add_argument('--first',type=int,default=1002)
p.add_argument('--last',type=int,default=1038,help='Exclusive end of timed frames')
a=p.parse_args()

def read(path):
    with path.open() as stream:return list(csv.DictReader(stream))

def stats(values):
    v=np.asarray(values,dtype=float)
    return dict(mean=float(v.mean()),p95=float(np.percentile(v,95)),max=float(v.max()))

report={}
for mode in a.modes:
    rows=read(a.results/(mode+'.csv'))
    log=(a.results/(mode+'.log')).read_text(errors='replace')
    if f'DONE frames={len(rows)} ' not in log:raise RuntimeError(mode+' did not finish')
    if 'PRELOAD first=1000 count=40 ' not in log:raise RuntimeError(mode+' has a different preload window')
    if len(rows)<=a.last:raise RuntimeError(mode+' lacks the whole timing window')
    window=rows[a.first:a.last]
    result=dict(frames=len(rows),window_frames=len(window),initialized_window=sum(r['initialized']=='1' for r in window))
    ive=re.search(r'FRONT_IVE flags=(\d+) quality=(\d+)',log)
    result['ive_flags']=int(ive[1]) if ive else 0
    result['ive_quality']=int(ive[2]) if ive else None
    if mode in ('off','hist','st','both') and result['ive_flags']!={'off':0,'hist':1,'st':2,'both':3}[mode]:
        raise RuntimeError(mode+' has unexpected IVE runtime flags')
    for key in rows[0]:
        if key.endswith('_ms') or key.startswith(('front_ive_','front_left_','front_right_','detail_lk_','detail_fast_candidates')):
            result[key]=stats([float(r[key]) for r in window])
    result['frontend_active_ms']=stats([float(r['front_total_ms'])-float(r['front_snapshot_wait_ms']) for r in window])
    trace=read(a.results/(mode+'-trace.csv'))
    events=[e for e in trace if e['kind']=='camera']
    if not events:raise RuntimeError(mode+' missing camera trace events')
    starts=[]
    for row in rows[a.first:a.last+1]:
        stamp=int(row['timestamp_ns'])*1e-9
        event=min(events,key=lambda e:abs(float(e['frame_stamp'])-stamp))
        if abs(float(event['frame_stamp'])-stamp)>1e-5:raise RuntimeError(mode+' missing timed camera')
        starts.append(float(event['begin_ms']))
    result['period_ms']=stats(np.diff(starts))
    result['fps']=1000/result['period_ms']['mean']
    if mode in ('hist','both') and result['front_ive_histogram_calls']['mean']!=2:raise RuntimeError(mode+' did not actually run stereo IVE hist')
    if mode in ('st','both') and sum(float(r['front_ive_st_calls']) for r in rows)==0:raise RuntimeError(mode+' did not actually run IVE ST')
    report[mode]=result
if 'off' in report:
    baseline=report['off']
    for mode,result in report.items():
        result['frontend_saved_ms']=baseline['frontend_active_ms']['mean']-result['frontend_active_ms']['mean']
        result['period_saved_ms']=baseline['period_ms']['mean']-result['period_ms']['mean']
        result['throughput_gain_percent']=(result['fps']/baseline['fps']-1)*100
target=a.results/'ive-comparison.json'
target.write_text(json.dumps(report,indent=2)+'\n')
for mode,r in report.items():
    print(f"{mode:6s} frontend={r['frontend_active_ms']['mean']:.3f} ms period={r['period_ms']['mean']:.3f} ms "
          f"p95={r['period_ms']['p95']:.3f} ms fps={r['fps']:.2f} initialized={r['initialized_window']}/{r['window_frames']} "
          f"points={r['front_left_points']['mean']:.1f}/{r['front_right_points']['mean']:.1f}")
print(target)
