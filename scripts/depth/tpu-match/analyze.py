#!/usr/bin/env python3
"""Summarize the final standalone TPU SAD experiments; exclude two warmup calls."""
import argparse
import csv
import json
import statistics
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('directory', type=Path)
a = p.parse_args()
result = {'scope': 'standalone integer SAD/WTA, not the complete depth mode',
          'warmup_calls_excluded': 2, 'runs': {}, 'verified_calls_including_warmup': 0}
for path in sorted(a.directory.glob('final-*.csv')):
    all_rows = list(csv.DictReader(path.open()))
    assert all_rows, f'Empty result: {path}'
    assert all(int(r['cost_mismatches']) == int(r['disparity_mismatches']) == 0 for r in all_rows), path
    result['verified_calls_including_warmup'] += len(all_rows)
    rows = [r for r in all_rows if int(r['run']) >= 0]
    def mean(key):
        return statistics.mean(float(r[key]) for r in rows)
    record = {'measured_calls': len(rows), 'width': int(rows[0]['width']), 'height': int(rows[0]['height']),
              'disparities': int(rows[0]['disparities']), 'case': rows[0]['case'],
              'aggregation': rows[0]['aggregation']}
    for key in ('prefilter_ms', 'upload_ms', 'execute_ms', 'download_ms', 'tpu_path_ms',
                'tpu_cpu_ms', 'cpu_sad_ms', 'all64_bm_ms', 'plane_correct_fraction',
                'bm_valid_pixels', 'wta_bm_integer_agreement', 'wta_positive_pixels'):
        record[key] = mean(key)
    record['prefilter_plus_tpu_wall_ms'] = mean('prefilter_ms') + mean('tpu_path_ms')
    record['prefilter_plus_tpu_cpu_estimate_ms'] = mean('prefilter_ms') + mean('tpu_cpu_ms')
    record['tpu_path_p95_ms'] = sorted(float(r['tpu_path_ms']) for r in rows)[int(.95*(len(rows)-1))]
    result['runs'][path.stem] = record
    print(path.stem, 'TPU incl IO:', round(record['tpu_path_ms'], 3),
          'prefilter+TPU:', round(record['prefilter_plus_tpu_wall_ms'], 3),
          'calling thread CPU:', round(record['tpu_cpu_ms'], 3),
          'full all64 BM:', round(record['all64_bm_ms'], 3))
assert result['runs'], 'No final-*.csv results'
(a.directory / 'summary.json').write_text(json.dumps(result, indent=2)+'\n')
