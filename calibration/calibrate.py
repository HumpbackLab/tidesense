#!/usr/bin/env python3
"""Calibrate TideSense stereo cameras and IMU from one complete raw recording."""
import argparse
from datetime import datetime
from pathlib import Path
import subprocess

import yaml

from tide_calibration.kalibr import LENSES
from tide_calibration.pipeline import calibrate

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='Complete tidesense_record recording directory')
    parser.add_argument('--imu', type=Path, required=True, help='Measured IMU noise/rate YAML; topic is set automatically')
    parser.add_argument('--lens', choices=LENSES, default='wide', help='wide: pinhole-radtan; fisheye: pinhole-equi')
    parser.add_argument('--target', type=Path, default=HERE / 'targets/aprilgrid.yaml')
    parser.add_argument('--output', type=Path, help='New result directory; existing results are never reused')
    args = parser.parse_args()
    try:
        source = args.source.resolve()
        output = args.output or source.with_name(source.name + '-calibration-' + datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
        calibrate(args.source, output, args.imu, args.target, args.lens)
    except KeyboardInterrupt:
        parser.exit(130, 'Calibration interrupted; logs retained. Use a new output directory to run again.\n')
    except (OSError, ValueError, TypeError, KeyError, ImportError, RuntimeError,
            subprocess.CalledProcessError, yaml.YAMLError) as exc:
        parser.exit(1, f'Calibration failed: {exc}\n')


if __name__ == '__main__':
    main()
