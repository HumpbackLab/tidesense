#!/usr/bin/env python3
"""Convert EuRoC cam0 PNG to lossless PGM for the minimal static OpenCV build."""
import argparse
from pathlib import Path
import shutil
from PIL import Image

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', type=Path)
parser.add_argument('destination', type=Path)
args = parser.parse_args()
src, dst = args.source.resolve(), args.destination.resolve()
if src == dst or src in dst.parents or dst in src.parents:
    raise SystemExit('Use separate, non-nested source and destination directories')
(dst/'mav0/cam0/data').mkdir(parents=True, exist_ok=True)
for name in ['imu0', 'state_groundtruth_estimate0']:
    shutil.copytree(src/'mav0'/name, dst/'mav0'/name, dirs_exist_ok=True)
shutil.copy2(src/'manifest.json', dst/'source-manifest.json')
shutil.copy2(src/'mav0/cam0/sensor.yaml', dst/'mav0/cam0/sensor.yaml')
(dst/'mav0/cam0/data.csv').write_text((src/'mav0/cam0/data.csv').read_text().replace('.png', '.pgm'))
for path in sorted((src/'mav0/cam0/data').glob('*.png')):
    image = Image.open(path)
    if image.mode != 'L':
        raise ValueError(f'Expected grayscale image: {path}')
    output = dst/'mav0/cam0/data'/path.with_suffix('.pgm').name
    image.save(output)
    if image.tobytes() != Image.open(output).tobytes():
        raise ValueError(f'Pixel mismatch: {path}')
print('Prepared', dst)
