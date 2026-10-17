#!/usr/bin/env python3
"""PC-only extraction of board-encoded H.264, for comparison with H.265 mvCol.

This is an analysis tool, not a demonstrated board-side MV interface.
Normal-P must reference the previous input frame; verify SPS/slice headers first.
"""
import argparse
from collections import Counter
import csv
import itertools
import json
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[4]

def validate_references(stream, out, frame_count):
    headers = out/'headers.log'
    with headers.open('w') as f:
        subprocess.run(['ffmpeg','-nostdin','-hide_banner','-loglevel','verbose',
                        '-i',str(stream),'-map','0:v:0','-c:v','copy',
                        '-bsf:v','trace_headers','-f','null','-'], stdout=f,stderr=f,check=True)
    fields = ['max_num_ref_frames','num_ref_idx_l0_default_active_minus1',
              'num_ref_idx_active_override_flag','ref_pic_list_modification_flag_l0',
              'adaptive_ref_pic_marking_mode_flag','long_term_reference_flag',
              'slice_type','gaps_in_frame_num_allowed_flag']
    text = headers.read_text()
    values = {f:dict(Counter(map(int,re.findall(r'\b'+f+r'\s+[01]+\s*=\s*(\d+)',text)))) for f in fields}
    for f in fields:
        expected = {5,7} if f=='slice_type' else {1} if f=='max_num_ref_frames' else {0}
        assert values[f] and set(values[f])<=expected, (f,values[f])
    assert sum(values['slice_type'].values()) == frame_count
    (out/'reference-validation.json').write_text(json.dumps(values,indent=2)+'\n')

def export(stream, timestamps, out):
    out.mkdir(parents=True, exist_ok=True)
    rows = [r for r in csv.reader(timestamps.open()) if r and not r[0].startswith('#')]
    validate_references(stream,out,len(rows))
    with (out/'vectors.csv').open('w') as f, (out/'decode.log').open('w') as e:
        subprocess.run([str(ROOT/'install/openvins-codec-mv/extract_mvs'), str(stream)],
                       stdout=f, stderr=e, check=True)
    decoded = int(re.search(r'DECODED_FRAMES=(\d+)', (out/'decode.log').read_text())[1])
    assert decoded == len(rows), (decoded,len(rows))
    shapes = Counter()
    counts = []
    with (out/'vectors.csv').open() as f, (out/'vectors.bin').open('wb') as dest:
        groups = iter(itertools.groupby(csv.DictReader(f), key=lambda r: int(r['framenum'])-1))
        index, group = next(groups, (None, ()))
        dest.write(b'CVMV0001')
        for frame, row in enumerate(rows):
            vectors = []
            if index == frame:
                for mv in group:
                    assert int(mv['source']) == -1
                    scale = int(mv['motion_scale'])
                    assert scale > 0
                    bx, by = int(mv['motion_x'])/scale, int(mv['motion_y'])/scale
                    w,h = int(mv['blockw']),int(mv['blockh'])
                    shapes[f'{w}x{h}'] += 1
                    vectors.append((int(mv['dstx'])+bx,int(mv['dsty'])+by,-bx,-by,w,h))
                index, group = next(groups, (None, ()))
            assert index is None or index > frame
            dest.write(struct.pack('<QQII', int(row[0]), int(rows[frame-1][0]) if frame else 0,
                                   frame,len(vectors)))
            for v in vectors: dest.write(struct.pack('<6f',*v))
            counts.append(len(vectors))
        assert index is None
    stats = dict(frames=len(rows),frames_with_vectors=sum(bool(n) for n in counts),
                 vector_count=sum(counts),mean_vectors_per_frame=sum(counts)/len(counts),
                 block_shapes=dict(shapes),decoded_frames=decoded,
                 extraction='PC FFmpeg decoder, actual SG2002 hardware H264 bitstream',
                 input_stream=str(stream),source_timestamps=str(timestamps))
    (out/'export.json').write_text(json.dumps(stats,indent=2)+'\n')
    print(json.dumps(stats,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('stream', type=Path)
    p.add_argument('timestamps', type=Path)
    p.add_argument('out', type=Path)
    a=p.parse_args()
    export(a.stream.resolve(),a.timestamps.resolve(),a.out.resolve())
