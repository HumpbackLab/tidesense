#!/usr/bin/env python3
"""Compare H264 complete bitstream partitions and current H265 mvCol interface."""
import csv
import json
from pathlib import Path
import re
import struct
import sys
sys.path.insert(0,'/usr/lib/python3/dist-packages')
import numpy as np
from shallow_summary import ROOT, align

WORK=ROOT/'install/openvins-codec-h264'
OLD=ROOT/'install/openvins-codec-shallow'

def read(path):
    return list(csv.DictReader(path.open()))

def cache_stats(path):
    counts=[]
    with path.open('rb') as f:
        assert f.read(8)==b'CVMV0001'
        while header:=f.read(24):
            _,_,i,n=struct.unpack('<QQII',header)
            assert i==len(counts)
            counts.append(n)
            assert len(f.read(n*24))==n*24
    nonempty=sum(bool(n) for n in counts)
    return dict(frames=len(counts),frames_with_vectors=nonempty,vector_count=sum(counts),
                vectors_per_P_frame=sum(counts)/nonempty)

def main():
    rows=read(ROOT/'install/fisheye-benchmark/openvins/uzh-input/stereo.csv')
    enc=re.findall(r'^ENCODED frame=(\d+) seq=(\d+) pts=(\d+)',(WORK/'encode.log').read_text(),re.M)
    # DictReader uses a commented CSV header; values retain original timestamps.
    assert len(enc)==len(rows)==1970
    for i,(a,b,c) in enumerate(enc):
        assert int(a)==int(b)==i and c==next(iter(rows[i].values()))
    sources={
      'h264':(WORK/'fpv-poses.csv',WORK/'fpv-detail.csv',WORK/'fpv/vectors.bin'),
      'h265':(OLD/'host-results/fpv-shallow-rep1-poses.csv',OLD/'host-results/fpv-shallow-rep1-detail.csv',OLD/'fpv/vectors.bin')}
    poses={name:read(paths[0]) for name,paths in sources.items()}
    lookups={name:{int(r['timestamp_ns']):r for r in rows if r['initialized']=='1'} for name,rows in poses.items()}
    gt=np.loadtxt(ROOT/'install/schurvins-gcc11-static/public-datasets/uzh-fpv/indoor_forward_6_replay/groundtruth.csv',delimiter=',')
    common=sorted(set.intersection(*[set(l) for l in lookups.values()]))
    common=[t for t in common if gt[0,0]<=t-16684572<=gt[-1,0]]
    t=np.array(common,dtype=float)-16684572
    truth=np.column_stack([np.interp(t,gt[:,0],gt[:,k]) for k in [1,2,3]])
    summary={}
    for name,(_,detail,cache) in sources.items():
        initialized=[r for r in poses[name] if r['initialized']=='1']
        stamps={r['timestamp_ns'] for r in initialized[1:]}
        details=[r for r in read(detail) if r['timestamp_ns'] in stamps]
        def total(field): return sum(int(r[field]) for r in details)
        points,seeded=total('points'),total('seeded')
        lookup=lookups[name]
        p=np.array([[float(lookup[t][k]) for k in ['px','py','pz']] for t in common])
        error=np.linalg.norm(align(p,truth)-truth,axis=1)
        states=np.array([[float(r[k]) for k in ['px','py','pz','qx','qy','qz','qw']] for r in initialized])
        summary[name]=dict(cache_stats(cache),initialized=len(initialized),finite=bool(np.isfinite(states).all()),
          tracking_calls=len(details),points=points,seeded_fraction=seeded/points,
          retry_fraction_of_seeded=total('full_retries')/seeded,full_fraction=total('full_points')/points,
          endpoint_m=float(np.linalg.norm(states[-1,:3]-states[0,:3])),
          ate=dict(rmse_m=float(np.sqrt(np.mean(error**2))),samples=len(common),
                   duration_s=(common[-1]-common[0])*1e-9,alignment='SE3 fixed scale'))
    summary['comparison']='H264 full partitions exported by PC decoder vs H265 16x16 collocated summary read directly on board; not equal export interfaces'
    (WORK/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps(summary,indent=2))

if __name__=='__main__': main()
