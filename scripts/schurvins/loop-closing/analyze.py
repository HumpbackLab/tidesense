#!/usr/bin/env python3
"""Summarize board A/B captures; capture cadence is not compute capacity."""
import argparse
import csv
import json
import re
import statistics
from pathlib import Path


def percentile(values, fraction):
    return sorted(values)[round((len(values) - 1) * fraction)] if values else None


def summarize(prefix, warmup):
    log = prefix.with_suffix('.log').read_text()
    with prefix.with_suffix('.profile.csv').open() as stream:
        all_rows = list(csv.DictReader(stream))
    rows = all_rows[warmup:]
    if len(rows) < 2:
        raise ValueError(f'{prefix}: too few frames')
    elapsed = (int(rows[-1]['timestamp_ns']) - int(rows[0]['timestamp_ns'])) * 1e-9
    wall = [(int(row['end_mono_ns']) - int(row['begin_mono_ns'])) * 1e-6 for row in rows]
    resources = []
    with prefix.with_suffix('.resources.csv').open() as stream:
        for row in csv.DictReader(stream):
            try:
                parsed = {key: float(value) for key, value in row.items()}
                if parsed['rss_kib'] > 0:
                    resources.append(parsed)
            except (ValueError, TypeError):
                continue
    first, last = resources[0], resources[-1]
    total = last['total_ticks'] - first['total_ticks']
    loops = []
    loop_file = prefix.with_suffix('.loop.csv')
    if loop_file.exists():
        with loop_file.open() as stream:
            loops = list(csv.DictReader(stream))
    loop_ms = [float(row['work_ms']) for row in loops]
    result = {
        'tag': prefix.name,
        'exit': int(prefix.with_suffix('.exit').read_text()),
        'frames': len(all_rows), 'warmup_frames': warmup, 'steady_frames': len(rows),
        'processed_fps': (len(rows) - 1) / elapsed,
        'tracking_frames': sum(int(row['tracking']) for row in all_rows),
        'vio_wall_mean_ms': statistics.mean(wall), 'vio_wall_p95_ms': percentile(wall, .95),
        'process_cpu_percent': 100 * ((last['user_ticks']+last['sys_ticks'])-(first['user_ticks']+first['sys_ticks'])) / total,
        'system_busy_percent': 100 * (1-(last['idle_ticks']-first['idle_ticks'])/total),
        'sampled_peak_rss_kib': max(row['hwm_kib'] for row in resources),
        'min_available_kib': min(row['available_kib'] for row in resources),
        'completed_processing_fps': float(re.search(r'completed_processing_fps=([\d.]+)',log)[1])
          if 'completed_processing_fps=' in log else None,
        'steady_execution_fps': (len(rows)-1) / ((int(rows[-1]['end_mono_ns'])-int(rows[0]['end_mono_ns']))*1e-9),
        'camera_replaced_pending': int(re.search(r'replaced_pending=(\d+)', log)[1])
          if 'replaced_pending=' in log else None,
        'loop_tasks': len(loops),
        'loop_admitted': sum(int(row['admitted']) for row in loops),
        'loop_queries': sum(int(row['queries']) for row in loops),
        'loop_bow_candidates': sum(int(row['bow_candidates']) for row in loops),
        'loop_geometry_checks': sum(int(row.get('geometry_checks', 0)) for row in loops),
        'loop_reused_orb': sum(int(row.get('reused_orb',0)) for row in loops),
        'loop_precheck_3d_rejects': sum(int(row.get('precheck_3d_rejects',0)) for row in loops),
        'loop_geometric_candidates': sum(int(row['geometric_candidates']) for row in loops),
        'loop_verified': sum(int(row['verified']) for row in loops),
        'loop_work_mean_ms': statistics.mean(loop_ms) if loop_ms else None,
        'loop_work_p95_ms': percentile(loop_ms, .95),
        'loop_orb_ms': sum(float(row.get('orb_ms',0)) for row in loops),
        'loop_bow_ms': sum(float(row.get('bow_ms',0)) for row in loops),
        'loop_matching_ms': sum(float(row.get('matching_ms',0)) for row in loops),
        'loop_geometry_ms': sum(float(row.get('geometry_ms',0)) for row in loops),
        'loop_offload_summary': next((line for line in log.splitlines() if line.startswith('loop_offload:')), None),
        'orb_profile': next((line for line in log.splitlines() if line.startswith('ORB_PROFILE ')), None),
        'ive_orb': next((line for line in log.splitlines() if line.startswith('ive_orb:')), None),
        'loop_summary': next((line for line in log.splitlines() if line.startswith('loop_closing: received=')), None),
        'loop_worker_nice': int(re.search(r'loop_worker: nice=(-?\d+)',log)[1])
          if re.search(r'loop_worker: nice=(-?\d+)',log) else None,
    }
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--warmup', type=int, default=50)
    args = parser.parse_args()
    results = [summarize(path.with_suffix(''), args.warmup) for path in sorted(args.directory.glob('*.exit'))
               if not path.with_suffix('.invalid').exists()]
    print(json.dumps(results, ensure_ascii=False, indent=2))
