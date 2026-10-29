#!/usr/bin/env python3
"""Summarize fixed-input A53 LK scheduling; hardware spans use calibrated C906 rdtime."""
import argparse
import csv
import json
from pathlib import Path
import re
import numpy as np

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('results',type=Path)
p.add_argument('--reference',type=Path,help='Synchronous TPU pose CSV for exact trajectory comparison')
a=p.parse_args()
keys=['initialized','px','py','pz','qx','qy','qz','qw']
reference=list(csv.DictReader(a.reference.open())) if a.reference else None
report={}
for mode in ['gate0','gate1']:
    path=a.results/(mode+'.csv')
    if not path.exists():continue
    rows=list(csv.DictReader(path.open()));ready=[r for r in rows if r['initialized']=='1']
    log=(a.results/(mode+'.log')).read_text(errors='replace')
    done=re.findall(r'DONE frames=(\d+).*?wall_s=([\d.]+)',log)
    if not done or len(rows)!=int(done[-1][0]):raise RuntimeError(mode+' incomplete')
    if not ready:raise RuntimeError(mode+' did not initialize')
    fields=['compute_ms','compute_main_cpu_ms','compute_process_cpu_ms',
        'tpu_kickoff_wait_ms','tpu_consume_wait_ms','tpu_backend_wall_ms','tpu_backend_cpu_ms',
        'tpu_worker_wall_ms','tpu_worker_cpu_ms','tpu_preprocess_ms','tpu_detection_ms',
        'tpu_c906_ms','tpu_tpu_device_ms','tpu_c906_backend_overlap_ms','tpu_tpu_backend_overlap_ms',
        'tpu_consume_c906_ms','tpu_consume_tpu_ms','tpu_remote_ms','tpu_overlap_ms']
    out={'frames':len(rows),'initialized':len(ready),'wall_s':float(done[-1][1]),
         'all_compute_mean_ms':float(np.mean([float(r['compute_ms'])for r in rows])),
         'initialized_mean_ms':{k:float(np.mean([float(r[k])for r in ready]))for k in fields},
         'initialized_p95_ms':{k:float(np.percentile([float(r[k])for r in ready],95))for k in
             ['compute_ms','tpu_kickoff_wait_ms','tpu_consume_wait_ms']}}
    if reference:
        out['poses_identical_to_reference']=len(rows)<=len(reference) and all(all(x[k]==y[k]for k in keys)for x,y in zip(rows,reference))
    cpu=re.findall(r'CPU (.*)',log)
    if cpu:out['whole_run_cpu_seconds']={k:float(v)for k,v in re.findall(r'(\w+)=([\d.]+)',cpu[-1])}
    traces=list(csv.DictReader((a.results/(mode+'-trace.csv')).open()))
    indexed={}
    for event in traces:
        for k in ['begin_ms','end_ms','cpu_ms','uncertainty_ms']:event[k]=float(event[k])
        indexed.setdefault(event['frame_stamp'],{}).setdefault(event['kind'],[]).append(event)
    first_ready=float(ready[0]['timestamp_ns'])*1e-9
    stage_cpu={}
    for kind in ['worker','preprocess','detection']:
        selected=[e for e in traces if e['kind']==kind and float(e['frame_stamp'])>=first_ready]
        stage_cpu[kind]=sum(e['cpu_ms']for e in selected)/len(ready)
    out['worker_stage_cpu_mean_ms']=stage_cpu
    out['worker_pack_protocol_other_cpu_mean_ms']=stage_cpu['worker']-stage_cpu['preprocess']-stage_cpu['detection']
    out['camera_main_plus_worker_cpu_mean_ms']=out['initialized_mean_ms']['compute_main_cpu_ms']+stage_cpu['worker']
    dispatch=[];return_delay=[];uncertainty=[]
    for group in indexed.values():
        hosts=group.get('host_request',[]);devices=group.get('c906',[])
        if len(hosts)!=len(devices):raise RuntimeError('Missing request/device trace pair')
        for host,device in zip(hosts,devices):
            error=device['uncertainty_ms'];uncertainty.append(error)
            before=device['begin_ms']-host['begin_ms'];after=host['end_ms']-device['end_ms']
            if before < -error-.05 or after < -error-.05:raise RuntimeError('Device clock outside round-trip bound')
            dispatch.append(before);return_delay.append(after)
    out['trace_requests']=len(dispatch)
    out['clock_uncertainty_max_ms']=max(uncertainty,default=0)
    for name,values in [('dispatch_delay',dispatch),('return_schedule_delay',return_delay)]:
        out[name+'_mean_ms']=float(np.mean(values));out[name+'_p95_ms']=float(np.percentile(values,95))
    mean=out['initialized_mean_ms']
    out['vio_process_cpu_fraction_in_compute_windows']=mean['compute_process_cpu_ms']/mean['compute_ms']
    out['c906_overlap_fraction']=mean['tpu_c906_backend_overlap_ms']/mean['tpu_c906_ms']
    out['tpu_call_overlap_fraction']=mean['tpu_tpu_backend_overlap_ms']/mean['tpu_tpu_device_ms']
    report[mode]=out
if len(report)==2:
    r0=list(csv.DictReader((a.results/'gate0.csv').open()));r1=list(csv.DictReader((a.results/'gate1.csv').open()))
    report['scheduling_pose_identity']=len(r0)==len(r1) and all(all(x[k]==y[k]for k in keys)for x,y in zip(r0,r1))
memory=a.results/'memory.csv'
if memory.exists():
    mem=list(csv.DictReader(memory.open()));sd=list(csv.DictReader((a.results/'gate0.csv').open()))
    first,last=1002,1038  # exclude two boundary frames of the 1000..1039 cached window
    comparison={'cached_range':[1000,1040],'measured_range':[first,last],'samples':last-first}
    for name,rows in [('memory',mem),('sd',sd)]:
        subset=rows[first:last]
        comparison[name]={k:float(np.mean([float(r[k])for r in subset]))for k in [
            'compute_ms','compute_main_cpu_ms','compute_process_cpu_ms','tpu_consume_wait_ms',
            'tpu_consume_c906_ms','tpu_consume_tpu_ms','tpu_backend_cpu_ms','tpu_worker_cpu_ms']}
        comparison[name]['process_cpu_fraction']=comparison[name]['compute_process_cpu_ms']/comparison[name]['compute_ms']
    if reference:comparison['memory_poses_identical']=all(all(x[k]==y[k]for k in keys)for x,y in zip(mem,reference))
    report['cached_window_comparison']=comparison
(a.results/'timing-summary.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
