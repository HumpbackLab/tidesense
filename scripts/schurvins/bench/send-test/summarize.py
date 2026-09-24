#!/usr/bin/env python3
import pathlib, sys, csv, statistics as st, json, hashlib, re
out=pathlib.Path(sys.argv[1]); runs=[]
def stats(values):
    values=sorted(values)
    return dict(mean=st.mean(values),p50=st.median(values),
                p95=values[int(.95*(len(values)-1))],p99=values[int(.99*(len(values)-1))],
                maximum=max(values),over50=sum(v>50 for v in values),n=len(values))
for p in sorted(out.glob('*-vio.csv')):
    name=p.name.removesuffix('-vio.csv'); mode=name.split('-',1)[1]
    rows=list(csv.DictReader(p.open()))
    send=list(csv.DictReader((out/(name+'-send.csv')).open()))
    if len(rows)!=501 or len(send)!=501: raise ValueError((name,len(rows),len(send)))
    log=(out/(name+'-run.log')).read_text()
    tx=dict((k,float(v)) for k,v in re.findall(r'(\w+)=([\d.]+)',log.split('SEND ')[-1])) if 'SEND ' in log else {}
    item=dict(name=name,mode=mode,tracking=sum(int(r['tracking']) for r in rows),
              pose_sha256=hashlib.sha256((out/(name+'-poses.csv')).read_bytes()).hexdigest(),
              vio=stats([float(r['vio_ms']) for r in rows[5:]]),
              vio_skip50=stats([float(r['vio_ms']) for r in rows[50:]]),
              step=stats([float(r['step_ms']) for r in send[5:]]),
              submit=stats([float(r['submit_ms']) for r in send[5:]]),
              thread_cpu=stats([float(r['thread_cpu_ms']) for r in send[5:]]),
              tx=tx,rx=json.loads((out/(name+'-receiver.json')).read_text()))
    runs.append(item)
groups={}
for mode in ['base','pose','image2','image5']:
    group=[r for r in runs if r['mode']==mode]
    if not group: continue
    groups[mode]=dict(runs=len(group),vio_mean_ms=st.mean(r['vio']['mean'] for r in group),
                     vio_run_means=[r['vio']['mean'] for r in group],
                     step_mean_ms=st.mean(r['step']['mean'] for r in group),
                     submit_mean_ms=st.mean(r['submit']['mean'] for r in group),
                     mean_run_p95_ms=st.mean(r['vio']['p95'] for r in group),
                     mean_run_p99_ms=st.mean(r['vio']['p99'] for r in group),
                     over50=sum(r['vio']['over50'] for r in group),
                     measured_frames=sum(r['vio']['n'] for r in group))
    groups[mode]['pooled_vio']=stats([float(row['vio_ms']) for r in group
        for row in list(csv.DictReader((out/(r['name']+'-vio.csv')).open()))[5:]])
for mode,g in groups.items():
    g['vio_increase_percent']=100*(g['vio_mean_ms']/groups['base']['vio_mean_ms']-1)
    g['step_increase_percent']=100*(g['step_mean_ms']/groups['base']['step_mean_ms']-1)
result=dict(excluded_initial_frames=5,runs=runs,groups=groups)
(out/'summary.json').write_text(json.dumps(result,indent=2))
print(json.dumps(groups,indent=2))
print('POSE HASHES',len(set(r['pose_sha256'] for r in runs)))
for r in runs: print(r['name'],r['tx'],r['rx'])
