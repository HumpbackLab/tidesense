#!/usr/bin/env python3
"""Verify board-exported motion against deterministic known-motion fixtures."""
import csv
import json
from pathlib import Path
import re
import statistics

ROOT = Path(__file__).resolve().parents[4]
WORK = ROOT / 'install/venc-mv-device-20261017'

def describe(values):
    return dict(mean=statistics.mean(values), median=statistics.median(values),
                minimum=min(values), maximum=max(values))

def expected_motion(name, frame, x, y, metadata):
    if name in ('split', 'quadrants'):
        if frame <= 3:
            return (0, 0)
        if name == 'split':
            return ((8, 4), (-6, 2))[x >= 320]
        return ((8, 4), (-6, 2), (3, -5), (-4, -3))[(y >= 200)*2 + (x >= 320)]
    expected = metadata[name]['expected'][frame]
    return expected['forward_dx_px'], expected['forward_dy_px']

def main():
    fixtures = json.loads((WORK / 'fixtures.json').read_text())
    results = {}
    for name, config in fixtures.items():
        directory = WORK / ('csv-' + name)
        log = (directory / 'run.log').read_text()
        encoded = re.findall(r'^ENCODED frame=(\d+) seq=(\d+) pts=(\d+) packs=(\d+) send_to_stream_ms=([\d.]+)', log, re.M)
        assert len(encoded) == config['frames'], name
        assert [int(r[1]) for r in encoded] == list(range(config['frames']))
        assert not (directory / '000-vectors.csv').exists(), 'I frame must not export stale vectors'
        result = dict(encoded_frames=len(encoded), exported_p_frames=0,
            send_to_stream_ms=describe([float(r[4]) for r in encoded[1:]]),
            diagnostic_snapshot_csv_ms=describe([float(s) for s in re.findall(r'^SNAPSHOT_MS frame=\d+ ms=([\d.]+)', log, re.M)]),
            frames=[])
        good_total = valid_total = 0
        for i in range(1, config['frames']):
            rows = list(csv.DictReader((directory / f'{i:03d}-vectors.csv').open()))
            assert len(rows) == 1000, (name, i, len(rows))
            # Re-encode the documented candidate bit fields from raw snapshots:
            # cross-check C/board parser against independent host integer extraction.
            raw = (directory / f'{i:03d}-VENC_0_H265_mvCol.bin').read_bytes()
            samples = []
            valid = good = 0
            for row in rows:
                x, y = int(row['current_x']), int(row['current_y'])
                assert int(row['frame']) == i and int(row['seq']) == i
                assert int(row['slot']) == i % 2
                offset = (i % 2) * 8960 + (((y//64)*10 + x//64)*4 + (y%64)//16)*16
                word = int.from_bytes(raw[offset:offset+16], 'little') >> (25*((x%64)//16))
                vy, vx, flag = word & 4095, (word >> 12) & 4095, (word >> 24) & 1
                if vx >= 2048: vx -= 4096
                if vy >= 2048: vy -= 4096
                assert (int(row['back_x_qpel']), int(row['back_y_qpel']), int(row['inter_flag'])) == (vx, vy, flag)
                if not flag or not (32 <= x < 608 and 32 <= y < 368):
                    continue
                if name in ('split', 'quadrants') and abs(x-320) < 32:
                    continue
                if name == 'quadrants' and abs(y-200) < 32:
                    continue
                observed = float(row['forward_dx_px']), float(row['forward_dy_px'])
                expected = expected_motion(name, i, x, y, fixtures)
                valid += 1
                good += observed == expected
                samples.append(observed)
            assert valid, (name, i)
            good_total += good; valid_total += valid
            median = tuple(statistics.median(v[c] for v in samples) for c in (0, 1))
            frame_result = dict(frame=i, sampled_cells=valid, exact_expected_cells=good,
                                exact_fraction=good/valid, median_forward_dx_dy=median)
            if name not in ('split', 'quadrants'):
                expected = expected_motion(name, i, 320, 200, fixtures)
                frame_result['expected_forward_dx_dy'] = expected
                assert median == expected, (name, i, median, expected)
            result['frames'].append(frame_result)
            result['exported_p_frames'] += 1
        result['interior_exact_fraction'] = good_total / valid_total
        result['interior_valid_cells'] = valid_total
        results[name] = result
        print(name, 'frames',len(encoded),'P exports',result['exported_p_frames'],
              'interior_exact_pct',round(100*result['interior_exact_fraction'],3),
              'P encode mean ms',round(result['send_to_stream_ms']['mean'],3))
    (WORK / 'device-results.json').write_text(json.dumps(results, indent=2) + '\n')

if __name__ == '__main__':
    main()
