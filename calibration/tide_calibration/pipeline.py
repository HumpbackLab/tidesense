"""One-pass orchestration using local modules and a single shared bag."""
import hashlib
import json
import math
import os
from pathlib import Path
import shutil

import yaml

from . import recording
from .kalibr import Runtime
from .report import summarize
from .schurvins import write_config

NOISE_KEYS = ('update_rate', 'accelerometer_noise_density', 'accelerometer_random_walk',
              'gyroscope_noise_density', 'gyroscope_random_walk')


def save_json(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def noise_config(path):
    document = yaml.safe_load(path.read_text())
    if not isinstance(document, dict):
        raise ValueError('IMU noise configuration must be a mapping')
    result = {'rostopic': '/imu0'}
    for key in NOISE_KEYS:
        value = document.get(key)
        if (isinstance(value, bool) or not isinstance(value, (int, float))
                or not math.isfinite(value) or value <= 0):
            raise ValueError(f'IMU {key} must be a measured positive finite number (not null)')
        result[key] = value
    if abs(result['update_rate'] - 200) > 10:
        raise ValueError('TideSense requires IMU sampling near 200 Hz')
    return result


def check_target(path):
    target = yaml.safe_load(path.read_text())
    if not isinstance(target, dict) or target.get('target_type') != 'aprilgrid':
        raise ValueError('Target must be an AprilGrid YAML')
    for key in ('tagRows', 'tagCols'):
        value = target.get(key)
        if type(value) is not int or value < 1:
            raise ValueError(f'Target {key} must be a positive integer')
    for key in ('tagSize', 'tagSpacing'):
        value = target.get(key)
        if (isinstance(value, bool) or not isinstance(value, (int, float))
                or not math.isfinite(value) or value <= 0):
            raise ValueError(f'Target {key} must be a positive finite number')

def calibrate(source, output, imu, target, lens):
    source = source.resolve(strict=True)
    output = output.resolve()
    imu = imu.resolve(strict=True)
    target = target.resolve(strict=True)
    if source == output or source in output.parents or output in source.parents:
        raise ValueError('Output must be outside the input recording')
    if output.exists():
        raise ValueError(f'Output already exists; choose a new directory: {output}')
    noise = noise_config(imu)
    check_target(target)
    if os.geteuid() == 0:
        raise ValueError('Use your normal user with rootless Podman, not sudo')
    runtime = Runtime()
    runtime.preflight()
    # Check host dependencies before creating a result directory or exporting a large bag.
    import numpy  # noqa: F401
    import zstandard  # noqa: F401
    import rosbags.rosbag1  # noqa: F401
    print('[1/4] Validate raw recording and export acquisition-time ROS1 bag', flush=True)
    capture = recording.validate(source)
    if capture['pairs'] < 20:
        raise ValueError('At least 20 stereo pairs are required; this is not a quality guarantee')
    output.mkdir()
    (output / 'INCOMPLETE').write_text('Calibration in progress\n')
    manifest = dict(source=str(source), lens=lens, imu_model='scale-misalignment',
                    status='running', stage='export', quality='requires_review')
    try:
        for src, name in ((imu, 'imu-original.yaml'), (target, 'target.yaml'),
                          (source / 'metadata.yaml', 'capture-metadata.yaml')):
            shutil.copyfile(src, output / name)
        (output / 'imu.yaml').write_text(yaml.safe_dump(noise, sort_keys=False))
        for name in ('camera-config.yaml', 'imu-source.yaml'):
            if (source / name).is_file():
                shutil.copyfile(source / name, output / name)
        manifest['input_sha256'] = {name: sha256(source / name)
                                    for name in ('frames.bin', 'imu.csv', 'metadata.yaml')}
        manifest['configuration_sha256'] = {name: sha256(output / name)
                                            for name in ('imu-original.yaml', 'imu.yaml', 'target.yaml')}
        manifest['code_sha256'] = {p.name: sha256(p) for p in sorted(Path(__file__).parent.glob('*.py'))}
        manifest['image_id'] = runtime.image
        save_json(output / 'container-image.json', runtime.image_info)
        save_json(output / 'calibration.json', manifest)
        recording.write_bag(source, output / 'dataset.bag')
        save_json(output / 'capture.json', capture)
        print('[2/4] Calibrate stereo cameras', flush=True)
        manifest['stage'] = 'cameras'
        save_json(output / 'calibration.json', manifest)
        runtime.solve(output, 'cameras', lens)
        print('[3/4] Calibrate camera–IMU with scale-misalignment', flush=True)
        manifest['stage'] = 'imucam'
        save_json(output / 'calibration.json', manifest)
        runtime.solve(output, 'imucam', lens)
        print('[4/4] Export SchurVINS configuration and summarize results', flush=True)
        manifest['stage'] = 'summary'
        save_json(output / 'calibration.json', manifest)
        chain = yaml.safe_load((output / 'dataset-camchain-imucam.yaml').read_text())
        imu_result = yaml.safe_load((output / 'dataset-imu.yaml').read_text())
        config, shifts = write_config(chain, imu_result, output / 'schurvins.yaml')
        manifest.update(summarize(output, lens, capture, config, shifts))
        manifest.update(status='complete', stage='complete')
        save_json(output / 'calibration.json', manifest)
        (output / 'INCOMPLETE').rename(output / 'COMPLETE')
    except (Exception, KeyboardInterrupt) as exc:
        (output / 'schurvins.yaml').unlink(missing_ok=True)
        manifest.update(status='failed', error=str(exc) or type(exc).__name__)
        save_json(output / 'calibration.json', manifest)
        raise
    print(f'Results: {output}\nReview: {output / "report.md"}\nConfiguration: {output / "schurvins.yaml"}')
    return manifest
