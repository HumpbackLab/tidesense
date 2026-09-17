#!/usr/bin/env python3
"""Filter perf-script samples to VIO intervals and aggregate period-weighted costs."""
import argparse,bisect,collections,csv,json,re,statistics
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('samples',type=Path);p.add_argument('intervals',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
rows=list(csv.DictReader(a.intervals.open()))
assert len(rows)==501, len(rows)
r=rows[5:];beg=[int(x['begin_mono_ns']) for x in r];end=[int(x['end_mono_ns']) for x in r]
wall=[(e-b)/1e6 for b,e in zip(beg,end)];cpu=[int(x['cpu_ns'])/1e6 for x in r]
header=re.compile(r'^([A-Z]+)\s+(\d+)\.(\d+):\s+(\d+)\s+cycles:')
frame=re.compile(r'^\s+([0-9a-f]+) (.+) \(([^()]*)\)$')
callers=collections.Counter();selfs=collections.Counter();inclusive=collections.Counter();kernel=collections.Counter();groups=collections.Counter();counts=collections.Counter();byframe=collections.defaultdict(collections.Counter)
# Mutually exclusive groups: each sample is assigned using its innermost matching frame.
patterns=[('FAST detect',r'(fast_corner_detect_|fast10DetectNEON)'),('FAST score',r'(fast_corner_score_|fast10ScoreNEON)'),('FAST NMS',r'fast_nonmax_3x3'),('Pyramid halfSample',r'vk::halfSample'),('Patch align2D',r'feature_alignment::align2D'),('Patch warpAffine',r'warp::warpAffine'),('Patch ZMSSD',r'(updateZMSSD|patch_score::ZMSSD)'),('Schur Solve/Update',r'SchurVINS::(Solve3|StateUpdate|StateCorrection)'),('IMU Prediction',r'SchurVINS::(Prediction|PredictionState|Forward)'),('Depth filter other',r'(DepthFilter::|depth_filter_utils::)'),('Reprojection other',r'(Reprojector::|reprojector_utils::|FrameHandlerBase::projectMapInFrame)'),('Backend other',r'(SchurVINS::|FrameHandlerBase::optimizePose)')]
patterns=[(n,re.compile(expr)) for n,expr in patterns]
def consume(sample):
 if sample is None:return
 mode,t,w,stack=sample;counts['total_samples']+=1
 i=bisect.bisect_right(beg,t)-1
 if i<0 or t>=end[i]:return
 counts['vio_samples']+=1;counts['vio_period']+=w
 if mode.startswith('K'):
  counts['kernel_samples']+=1;counts['kernel_period']+=w
  kernel[stack[0][0] if stack else '[missing]']+=w;return
 if not mode.startswith('U'):
  counts['other_mode_samples']+=1;return
 counts['user_samples']+=1;counts['user_period']+=w
 leaf=stack[0][0] if stack else '[missing]';selfs[leaf]+=w
 if leaf in ('[unknown]','[missing]'):counts['unresolved_user_period']+=w
 syms=[s for s,dso in stack]
 if ('feature_alignment::align2D' in leaf or 'warp::warpAffine' in leaf) and len(syms)>1:
  callers[leaf+' <- '+syms[1]]+=w
 if any('FrameHandlerBase::addImageBundle' in s for s in syms):counts['root_present_period']+=w
 for sym in set(syms):inclusive[sym]+=w
 group='Other user'
 for sym in syms:
  found=next((name for name,pat in patterns if pat.search(sym)),None)
  if found:group=found;break
 groups[group]+=w;byframe[i][group]+=w
current=None
for line in a.samples.open():
 m=header.match(line)
 if m:
  consume(current)
  t=int(m[2])*10**9+int(m[3].ljust(9,'0'))
  current=(m[1],t,int(m[4]),[])
 elif current:
  m=frame.match(line)
  if m:current[3].append((m[2],m[3]))
consume(current)
def table(counter,total):
 return [dict(symbol=s,period=w,percent=100*w/total) for s,w in counter.most_common()]
u=counts['user_period'];assert u>0
result=dict(method='cycles, 199 Hz, CLOCK_MONOTONIC; first 5 VIO calls excluded; period-weighted; self vs inclusive kept separate',frames=len(r),wall_mean_ms=statistics.mean(wall),cpu_mean_ms=statistics.mean(cpu),cpu_wall_ratio=sum(cpu)/sum(wall),wall_total_ms=sum(wall),cpu_total_ms=sum(cpu),counts=dict(counts),self=table(selfs,u),hot_callers=table(callers,u),inclusive=table(inclusive,u),exclusive_groups=table(groups,u),kernel_self=table(kernel,counts['kernel_period']) if counts['kernel_period'] else [])
a.output.write_text(json.dumps(result,indent=2)+'\n')
print(f"frames={len(r)} wall={result['wall_mean_ms']:.3f} ms cpu={result['cpu_mean_ms']:.3f} ms user_samples={counts['user_samples']} kernel_samples={counts['kernel_samples']} root_coverage={100*counts['root_present_period']/u:.2f}%")
print('Top self:')
for x in result['self'][:18]:print(f"{x['percent']:6.2f}% {x['symbol'][:180]}")
print('Exclusive groups:')
for x in result['exclusive_groups']:print(f"{x['percent']:6.2f}% {x['symbol']}")
