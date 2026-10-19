#!/usr/bin/env python3
"""Rerun an existing converted camera/IMU bag with Kalibr's full intrinsic model."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time


def sha256(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True,
                        help='Existing converted calibration directory with imucam.bag and three input YAMLs')
    parser.add_argument('--output', type=Path, required=True, help='New result directory')
    parser.add_argument('--image', default='localhost/tidesense-kalibr:noetic')
    parser.add_argument('--max-iter', type=int, default=30)
    parser.add_argument('--in-place', action='store_true',
                        help='Use the existing converted directory; refuse to overwrite calibration outputs')
    parser.add_argument('--disable-g-sensitivity', action='store_true',
                        help='Controlled Python-source override: fix the gyro acceleration-sensitivity matrix to zero')
    args = parser.parse_args()
    if args.max_iter < 1:
        parser.error('--max-iter must be positive')
    source = args.source.resolve(strict=True)
    output = args.output.resolve()
    names = ('imucam.bag', 'camchain-input.yaml', 'imu-input.yaml', 'target.yaml')
    for name in names:
        if not (source / name).is_file():
            parser.error(f'Missing input: {source / name}')
    if args.in_place:
        if output != source:
            parser.error('--in-place requires source and output to be the same directory')
        if any((output / name).exists() for name in ('run.json', 'imucam-imu.yaml', 'imucam-camchain-imucam.yaml')):
            parser.error('Calibration output already exists')
    else:
        output.mkdir(parents=True, exist_ok=False)
    manifest = dict(source=str(source), output=str(output), status='preparing',
                    model='scale-misalignment', input_values_changed=False,
                    gyro_g_sensitivity_estimated=not args.disable_g_sensitivity,
                    input_policy='Values are unchanged; runtime export requires nominal raw SI measurements.',
                    input_sha256={n: sha256(source / n) for n in names})
    save(output / 'run.json', manifest)
    if not args.in_place:
        subprocess.run(['cp', '--reflink=auto', str(source / 'imucam.bag'), str(output / 'imucam.bag')], check=True)
        for name in names[1:]:
            shutil.copy2(source / name, output / name)
    if {n: sha256(output / n) for n in names} != manifest['input_sha256']:
        raise RuntimeError('Copied input hashes differ')
    inspect = subprocess.check_output(['podman', 'image', 'inspect', args.image], text=True)
    (output / 'container-image.json').write_text(inspect)
    extra_mount = []
    if args.disable_g_sensitivity:
        module = '/catkin_ws/src/kalibr/aslam_offline_calibration/kalibr/python/kalibr_imu_camera_calibration/IccSensors.py'
        original = subprocess.check_output(['podman', 'run', '--rm', '--network=none', args.image, 'cat', module], text=True)
        needle = 'self.M_accel_gyro_Dv.setActive(True)'
        if original.count(needle) != 1:
            raise RuntimeError('Expected exactly one gyro acceleration-sensitivity activation site')
        patched = original.replace(needle, 'self.M_accel_gyro_Dv.setActive(False)')
        (output / 'IccSensors-original.py').write_text(original)
        (output / 'IccSensors-zero-g-sensitivity.py').write_text(patched)
        extra_mount = ['--volume', f'{output / "IccSensors-zero-g-sensitivity.py"}:{module}:ro']
        manifest['python_override'] = dict(container_path=module,
                                          original_sha256=sha256(output / 'IccSensors-original.py'),
                                          patched_sha256=sha256(output / 'IccSensors-zero-g-sensitivity.py'),
                                          change='Fix the initially zero M_accel_gyro design variable inactive; all other source is unchanged.')
    command = ['podman', 'run', '--rm', '--init', '--network=none', '--cap-drop=all',
               '--security-opt=no-new-privileges', '--env', 'PYTHONUNBUFFERED=1',
               '--env', 'OMP_NUM_THREADS=2', '--env', 'OPENBLAS_NUM_THREADS=1',
               '--volume', f'{output}:/output:rw', *extra_mount, '--workdir', '/output', args.image,
               'xvfb-run', '-a', 'rosrun', 'kalibr', 'kalibr_calibrate_imu_camera',
               '--bag', '/output/imucam.bag', '--cams', '/output/camchain-input.yaml',
               '--imu', '/output/imu-input.yaml', '--target', '/output/target.yaml',
               '--imu-models', 'scale-misalignment', '--max-iter', str(args.max_iter),
               '--dont-show-report']
    manifest.update(status='running', command=command,
                    runner_sha256=sha256(Path(__file__).resolve()))
    save(output / 'run.json', manifest)
    print(json.dumps(manifest, indent=2), flush=True)
    start = time.monotonic()
    with (output / 'kalibr.log').open('w') as log:
        process = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    results = ('imucam-imu.yaml', 'imucam-camchain-imucam.yaml',
               'imucam-results-imucam.txt', 'imucam-report-imucam.pdf')
    manifest.update(returncode=process.returncode, wall_s=time.monotonic() - start,
                    status='complete' if process.returncode == 0 and
                    all((output / n).is_file() and (output / n).stat().st_size > 0 for n in results) else 'failed',
                    result_sha256={n: sha256(output / n) for n in results if (output / n).is_file()},
                    source_inputs_unchanged=all(sha256(source / n) == manifest['input_sha256'][n] for n in names))
    save(output / 'run.json', manifest)
    print(json.dumps(manifest, indent=2), flush=True)
    if manifest['status'] != 'complete' or not manifest['source_inputs_unchanged']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
