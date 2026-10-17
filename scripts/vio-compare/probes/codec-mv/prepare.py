#!/usr/bin/env python3
"""Encode causal single-reference H.264 and export indexed full-precision MVs."""
import argparse
import csv
import itertools
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[4]
WORK = ROOT / 'install/openvins-codec-mv'
DATASETS = {
    'tide': (ROOT / 'install/replay-20261012-084638/record', 20),
    'fpv': (ROOT / 'install/fisheye-benchmark/openvins/uzh-input', 30),
}

def prepare(name, crf):
    source, fps = DATASETS[name]
    out = WORK / name
    out.mkdir(parents=True, exist_ok=True)
    rows = [r for r in csv.reader((source / 'stereo.csv').open()) if r and not r[0].startswith('#')]
    images = [str(source / r[1]) for r in rows]
    assert all("'" not in p and '\n' not in p for p in images)
    (out / 'images.txt').write_text(''.join("file '" + p + "'\n" for p in images))
    command = [
        '/usr/bin/ffmpeg', '-nostdin', '-hide_banner', '-loglevel', 'warning', '-y',
        '-r', str(fps), '-f', 'concat', '-safe', '0', '-i', str(out / 'images.txt'),
        '-vsync', '0', '-frames:v', str(len(rows)), '-c:v', 'libx264', '-threads', '1',
        '-preset', 'fast', '-tune', 'zerolatency', '-bf', '0', '-refs', '1',
        '-g', '60', '-keyint_min', '60', '-sc_threshold', '0',
        '-x264-params', 'sliced-threads=0:sync-lookahead=0:rc-lookahead=0:merange=64',
        '-crf', str(crf), '-pix_fmt', 'yuv420p', '-progress', 'pipe:1', str(out / 'left.h264'),
    ]
    start = time.monotonic()
    with (out / 'encode-progress.log').open('w') as f, (out / 'encode.log').open('w') as e:
        subprocess.run(command, stdout=f, stderr=e, check=True)
    encode_s = time.monotonic() - start
    encoded_count = int(re.findall(r'^frame=(\d+)', (out / 'encode-progress.log').read_text(), re.M)[-1])
    assert encoded_count == len(rows)
    start = time.monotonic()
    with (out / 'vectors.csv').open('w') as f, (out / 'decode.log').open('w') as e:
        subprocess.run([str(WORK / 'extract_mvs'), str(out / 'left.h264')], stdout=f, stderr=e, check=True)
    decode_s = time.monotonic() - start
    decoded_count = int(re.search(r'DECODED_FRAMES=(\d+)', (out / 'decode.log').read_text())[1])
    assert decoded_count == len(rows)
    # Each record maps one raw frame to MVs from precisely the previous raw frame.
    # No B frames, no extra references, no future-frame lookahead are used.
    count = predicted = nonzero = 0
    with (out / 'vectors.csv').open() as f, (out / 'vectors.bin').open('wb') as dest:
        groups = iter(itertools.groupby(csv.DictReader(f), key=lambda r: int(r['framenum']) - 1))
        group_index, group = next(groups, (None, ()))
        dest.write(b'CVMV0001')
        for index, row in enumerate(rows):
            vectors = []
            if group_index == index:
                for mv in group:
                    assert int(mv['source']) == -1, 'only past single-reference vectors are allowed'
                    scale = int(mv['motion_scale'])
                    assert scale > 0
                    bx, by = int(mv['motion_x']) / scale, int(mv['motion_y']) / scale
                    # Decoder convention is current->reference. Store source-frame footprint
                    # and forward displacement, so LK predicts previous->current positions.
                    vectors.append((int(mv['dstx']) + bx, int(mv['dsty']) + by,
                                    -bx, -by, int(mv['blockw']), int(mv['blockh'])))
                    nonzero += bx != 0 or by != 0
                group_index, group = next(groups, (None, ()))
            assert group_index is None or group_index > index
            dest.write(struct.pack('<QQII', int(row[0]), int(rows[index - 1][0]) if index else 0,
                                   index, len(vectors)))
            for vector in vectors:
                dest.write(struct.pack('<6f', *vector))
            count += len(vectors)
            predicted += bool(vectors)
        assert group_index is None
    stats = dict(dataset=name, frames=len(rows), frames_with_vectors=predicted, vector_count=count,
                 nonzero_vectors=nonzero, encode_wall_s=encode_s, decode_export_wall_s=decode_s,
                 codec='PC libx264, single thread, CRF ' + str(crf),
                 command=command, encoding_included_in_vio_timer=False,
                 time_policy='Original timestamps preserved; current P frame references exactly previous input frame')
    (out / 'encoding.json').write_text(json.dumps(stats, indent=2) + '\n')
    shutil.copytree(ROOT / f'install/openvins-mono-front/config-{name}', out / 'config', dirs_exist_ok=True)
    print(json.dumps(stats), flush=True)

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--datasets', nargs='+', choices=list(DATASETS), default=list(DATASETS))
    p.add_argument('--crf', type=int, default=20)
    args = p.parse_args()
    WORK.mkdir(exist_ok=True)
    src = Path(__file__).with_name('extract_mvs.c').read_text()
    src = src.replace('decode_packet(NULL);', 'decode_packet(NULL);\n    fprintf(stderr, "DECODED_FRAMES=%d\\n", video_frame_count);')
    (WORK / 'extract_mvs.c').write_text(src)
    subprocess.run(['cc', '-O2', str(WORK / 'extract_mvs.c'), '-o', str(WORK / 'extract_mvs'),
                    '-lavformat', '-lavcodec', '-lavutil'], check=True)
    for name in args.datasets:
        prepare(name, args.crf)
