#!/usr/bin/env python3
"""Check board H264 snapshots and observed context values against controlled inputs."""
import csv
import json
from pathlib import Path
import re
import sys
sys.path.insert(0,'/usr/lib/python3/dist-packages')
import numpy as np

ROOT=Path(__file__).resolve().parents[4]
WORK=ROOT/'install/venc-h264-direct-20261017'

def main():
    metadata=json.loads((ROOT/'install/venc-mv-device-20261017/fixtures.json').read_text())
    result={'raw_snapshots':{},'context':{}}
    for case,frames in [('shift-8-4',16),('dynamic',12),('quadrants',12)]:
        folder=WORK/case
        encoded=re.findall(r'^H264_ENCODED frame=(\d+) seq=(\d+)',(folder/'run.log').read_text(),re.M)
        assert len(encoded)==frames and all(int(a)==int(b)==i for i,(a,b) in enumerate(encoded))
        files=sorted(folder.glob('*WorkBuffer*.bin'))
        assert len(files)==frames
        assert len(list(folder.glob('*ReconFrameBuf*.bin')))==2*frames
        data=np.stack([np.fromfile(f,dtype='<u4') for f in files])
        assert data.shape==(frames,81920//4)
        changes=np.flatnonzero(np.any(data!=data[0],axis=0))
        result['raw_snapshots'][case]=dict(frames=frames,work_bytes=data.shape[1]*4,
            work_words_changing=len(changes),last_changing_work_byte_offset=int(changes[-1]*4),
            recon_buffers_per_frame=2,recon_bytes=720896)
    for case in ['dynamic','quadrants','quarter','bottom-right','top-left']:
        folder=WORK/f'context-{case}'
        rows=list(csv.DictReader((folder/'words.csv').open()))
        assert len(rows)==48
        assert {(int(r['frame']),int(r['candidate'])) for r in rows}=={(f,c) for f in range(12) for c in range(4)}
        assert all(int(r['frame'])==int(r['seq']) and int(r['is_P'])==int(int(r['frame'])!=0) for r in rows)
        candidate=[r for r in rows if r['candidate']=='1' and r['is_P']=='1']
        observed=np.array([[float(r['forward_dx_px']),float(r['forward_dy_px'])] for r in candidate])
        if case in ['dynamic','quarter']:
            expected=np.array([[metadata[case]['expected'][int(r['frame'])][k] for k in ['forward_dx_px','forward_dy_px']] for r in candidate])
        else:
            expected=np.array([[-4,-3] if int(r['frame'])>=4 and case in ['quadrants','bottom-right'] else [0,0] for r in candidate])
        error=np.linalg.norm(observed-expected,axis=1)
        assert np.max(error)==0,(case,error)
        result['context'][case]=dict(frames=12,context_vectors_per_frame=4,
            word_0x404_max_error_against_test_expectation_px=float(np.max(error)),
            word_0x404_test_expectation='whole-image displacement' if case in ['dynamic','quarter'] else 'bottom-right patch displacement',
            mean_read_16bytes_us=float(np.mean([float(r['read_ms'])*1000 for r in rows])),
            moving_frames_word_0x404_px=observed[3:].tolist())
    result['interpretation']='Four motion-related context words validated, no dense per-partition MV export established. Offset 0x404 follows bottom-right-local motion, not global motion.'
    (WORK/'probe-summary.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()
