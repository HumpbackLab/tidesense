#!/usr/bin/env python3
"""Choose IVE qualities from fixed-image ORB_POINTS logs; never runs FAST online."""
import argparse
import json
import re
from pathlib import Path


def read_points(path):
    text = path.read_text()
    levels = {}
    for level, calls, raw, border, retained, final in re.findall(
            r'ORB_POINTS level=(\d+) calls=(\d+) raw=(\d+) border=(\d+) retained=(\d+) final=(\d+)', text):
        levels[int(level)] = dict(zip(('raw', 'border', 'retained', 'final'),
                                     (int(v) / int(calls) for v in (raw, border, retained, final))))
    if set(levels) != set(range(8)):
        raise ValueError(f'{path}: expected all eight pyramid levels')
    detected = int(re.search(r'ORB_RESULT detected=(\d+)', text)[1])
    if abs(sum(levels[i]['final'] for i in levels) - detected) > 1e-6:
        raise ValueError(f'{path}: final count does not agree with ORB_RESULT')
    return levels


def compare(reference, actual):
    total = sum(level['final'] for sample in reference.values() for level in sample.values())
    border_total = sum(level['border'] for sample in reference.values() for level in sample.values())
    return {
        'mean_final': sum(v['final'] for sample in actual.values() for v in sample.values()) / len(actual),
        'final_relative_mae': sum(abs(sum(actual[s][i]['final'] for i in range(8)) -
                                      sum(reference[s][i]['final'] for i in range(8))) for s in reference) / max(1, total),
        'level_relative_mae': sum(abs(actual[s][i]['final'] - reference[s][i]['final'])
                                  for s in reference for i in range(8)) / max(1, total),
        'border_relative_mae': sum(abs(actual[s][i]['border'] - reference[s][i]['border'])
                                   for s in reference for i in range(8)) / max(1, border_total),
        'mean_final_by_level': [sum(actual[s][i]['final'] for s in reference) / len(reference) for i in range(8)],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directories', type=Path, nargs='+')
    parser.add_argument('--prefix', default='left-')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    reference, candidates = {}, {}
    for directory in args.directories:
        for path in sorted(directory.glob('*.log')):
            if not path.name.startswith(args.prefix):
                continue
            match = re.fullmatch(r'(.+)-(fast|q(\d+))\.log', path.name)
            if not match:
                continue
            sample, kind, quality = match.groups()
            if kind == 'fast':
                reference[sample] = read_points(path)
            else:
                candidates.setdefault(int(quality), {})[sample] = read_points(path)
    if not reference:
        parser.error('No FAST reference logs')
    candidates = {q: {s: values[s] for s in reference} for q, values in candidates.items()
                  if set(reference) <= set(values)}
    if not candidates:
        parser.error('No quality has all reference samples')
    scores = {q: compare(reference, values) for q, values in candidates.items()}
    scalar = min(scores, key=lambda q: (scores[q]['final_relative_mae'], scores[q]['level_relative_mae'],
                                        scores[q]['border_relative_mae'], -q))
    result = {'samples': sorted(reference), 'fast': compare(reference, reference),
              'selected_scalar': scalar, 'scalar_scores': scores}
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print('scalar:', scalar)
    print(json.dumps(scores[scalar], indent=2))


if __name__ == '__main__':
    main()
