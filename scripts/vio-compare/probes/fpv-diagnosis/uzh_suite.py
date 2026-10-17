#!/usr/bin/env python3
"""Run fixed SchurVINS variants on every applicable official UZH-FPV sequence."""
import argparse
import copy
import csv
from decimal import Decimal, ROUND_HALF_EVEN
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time
import zipfile

import cv2
import numpy as np
import yaml

from replay_ab import NOISE_KEYS, evaluate, read_yaml, rows, sha256
from uzh_download import atomic_json

class IndentedDumper(yaml.SafeDumper):
    def increase_indent(self, flow=False, indentless=False):
        return super().increase_indent(flow, False)


def opencv_yaml(value):
    return '%YAML:1.0\n' + yaml.dump(value, Dumper=IndentedDumper, default_flow_style=None, sort_keys=False)


VARIANTS = {'baseline': {}, 'matched-imu': {},
            'matched-imu-pixel-stable': {'SCHUR_STABLE_UPDATE': '1', 'SCHUR_PIXEL_RESIDUAL': '1'}}


def ns(value):
    return int((Decimal(value) * 1000000000).to_integral_value(rounding=ROUND_HALF_EVEN))


def text_rows(archive, name):
    return [line.split() for line in archive.read(name).decode().splitlines()
            if line.strip() and not line.startswith('#')]


def write_csv(path, header, values):
    with path.open('w') as stream:
        stream.write('#' + header + '\n')
        csv.writer(stream, lineterminator='\n').writerows(values)


def calibration(cache, item):
    name = 'race_calibration' if item['group'] == 'splits' else item['group'] + '_calib_snapdragon'
    with zipfile.ZipFile(cache / (name + '.zip')) as archive:
        chain_name = next(n for n in archive.namelist() if 'camchain-imucam' in n and n.endswith('.yaml'))
        imu_name = next(n for n in archive.namelist() if n.endswith('/imu.yaml'))
        chain = yaml.safe_load(archive.read(chain_name))
        imu = yaml.safe_load(archive.read(imu_name))
    return chain, imu


def native_calibration(chain, imu, name):
    cameras = []
    for key in ('cam0', 'cam1'):
        c = chain[key]
        transform = np.array(c['T_cam_imu'])
        if not np.allclose(transform[:3, :3].T @ transform[:3, :3], np.eye(3), atol=1e-6):
            raise ValueError('Invalid camera rotation')
        cameras.append({'camera': {'label': key, 'id': name + '-' + key,
            'line-delay-nanoseconds': 0, 'image_height': c['resolution'][1],
            'image_width': c['resolution'][0], 'type': c['camera_model'],
            'intrinsics': {'rows': 4, 'cols': 1, 'data': c['intrinsics']},
            'distortion': {'type': c['distortion_model'], 'parameters':
                {'rows': len(c['distortion_coeffs']), 'cols': 1, 'data': c['distortion_coeffs']}}},
            'T_B_C': {'rows': 4, 'cols': 4, 'data': np.linalg.inv(transform).ravel().tolist()}})
    return {'label': 'UZH-FPV_' + name, 'id': name, 'cameras': cameras,
        'imu_params': {'delay_imu_cam': 0., 'max_imu_delta_t': .01, 'acc_max': 200.,
            'omega_max': 35., 'sigma_omega_c': imu['gyroscope_noise_density'],
            'sigma_acc_c': imu['accelerometer_noise_density'],
            'sigma_omega_bias_c': imu['gyroscope_random_walk'],
            'sigma_acc_bias_c': imu['accelerometer_random_walk'],
            'sigma_integration': 0., 'g': 9.80665, 'imu_rate': int(imu['update_rate'])},
        'imu_initialization': {'velocity': [0., 0., 0.], 'omega_bias': [0., 0., 0.],
            'acc_bias': [0., 0., 0.], 'velocity_sigma': 2., 'omega_bias_sigma': .01, 'acc_bias_sigma': .1}}


def prepare(cache, item, out, image_dir, noise):
    chain, imu_config = calibration(cache, item)
    if len(chain) != 2:
        return {'status': 'unsupported', 'reason': 'Official camera/IMU calibration contains one camera; the native SchurVINS entry requires stereo.'}
    out.mkdir(parents=True, exist_ok=True)
    native = native_calibration(chain, imu_config, item['name'])
    (out / 'calibration.yaml').write_text(yaml.safe_dump(native, sort_keys=False))
    matched = copy.deepcopy(native)
    for dst, src in NOISE_KEYS.items():
        matched['imu_params'][dst] = float(noise[src])
    (out / 'matched-imu.yaml').write_text(yaml.safe_dump(matched, sort_keys=False))
    shift = round(chain['cam0']['timeshift_cam_imu'] * 1e9)
    image_dir.mkdir()
    image_link = out / 'images'
    if image_link.is_symlink():
        image_link.unlink()
    image_link.symlink_to(image_dir, target_is_directory=True)
    with zipfile.ZipFile(cache / (item['name'] + '.zip')) as archive:
        names = archive.namelist()
        left = text_rows(archive, 'left_images.txt')
        right = text_rows(archive, 'right_images.txt')
        inertial = text_rows(archive, 'imu.txt')
        gt = text_rows(archive, 'groundtruth.txt') if 'groundtruth.txt' in names else []
        left_by_time = {ns(v[1]): v for v in left}
        right_by_time = {ns(v[1]): v for v in right}
        if len(left_by_time) != len(left) or len(right_by_time) != len(right):
            raise ValueError('Duplicate camera timestamps')
        matched_times = sorted(set(left_by_time) & set(right_by_time))
        if not matched_times:
            raise ValueError('No exactly synchronized stereo timestamps')
        imu = [[ns(v[1]), *v[2:]] for v in inertial]
        stamps = [v[0] for v in imu]
        if not all(b > a for a, b in zip(stamps, stamps[1:])):
            raise ValueError('Non-increasing IMU timestamps')
        write_csv(out / 'imu.csv', 'timestamp_ns,gx,gy,gz,ax,ay,az', imu)
        if gt:
            write_csv(out / 'groundtruth.csv', 'timestamp_ns,px,py,pz,qx,qy,qz,qw', [[ns(v[0]), *v[1:]] for v in gt])
        elif item['public_gt']:
            raise ValueError('Official public-GT sequence has no groundtruth.txt')
        stereo = []
        dropped = 0
        for raw_time in matched_times:
            l, r = left_by_time[raw_time], right_by_time[raw_time]
            stamp = raw_time + shift
            if not (stamps[0] < stamp < stamps[-1]):
                dropped += 1
                continue
            pair = []
            for camera_id, entry in enumerate((l, r)):
                image = cv2.imdecode(np.frombuffer(archive.read(entry[2]), np.uint8), cv2.IMREAD_GRAYSCALE)
                expected = tuple(reversed(chain['cam' + str(camera_id)]['resolution']))
                if image is None or image.shape != expected:
                    raise ValueError('Decoded image dimensions disagree with calibration')
                image_name = Path(entry[2]).stem + '.pgm'
                if not cv2.imwrite(str(image_dir / image_name), image):
                    raise OSError('Cannot write replay image')
                pair.append('images/' + image_name)
            stereo.append([stamp, *pair])
        write_csv(out / 'stereo.csv', 'timestamp_ns,left,right', stereo)
    metadata = {'status': 'prepared', 'name': item['name'], 'group': item['group'],
        'source': item['url'], 'source_left_images': len(left), 'source_right_images': len(right),
        'exactly_paired_images': len(matched_times),
        'unpaired_left_images': len(left)-len(matched_times),
        'unpaired_right_images': len(right)-len(matched_times), 'image_pairs': len(stereo),
        'excluded_without_imu_coverage': dropped, 'imu_samples': len(imu), 'gt_samples': len(gt),
        'camera_offset_ns_applied_once': shift,
        'right_offset_difference_ns': round((chain['cam1']['timeshift_cam_imu']-chain['cam0']['timeshift_cam_imu'])*1e9),
        'imu_mean_rate_hz': (len(stamps)-1)*1e9/(stamps[-1]-stamps[0]),
        'imu_median_interval_ns': float(np.median(np.diff(stamps))),
        'image_span_s': (stereo[-1][0]-stereo[0][0])*1e-9,
        'conversion': 'PNG to mono8 PGM; no resize, rectification or image filtering',
        'calibration_sha256': sha256(out / 'calibration.yaml'), 'matched_imu_sha256': sha256(out / 'matched-imu.yaml')}
    atomic_json(out / 'manifest.json', metadata)
    return metadata


def health(out, stereo):
    poses = rows(out / 'poses.csv')
    values = np.array([[float(v) for v in row[1:8]] for row in poses])
    expected = [int(row[0]) for row in stereo]
    actual = [int(row[0]) for row in poses]
    complete = actual == expected
    finite = bool(values.size and np.isfinite(values).all())
    log = (out / 'run.log').read_text(errors='replace')
    obs = [int(v) for v in re.findall(r'nObs = (\d+)', log)]
    result = {'pose_rows': len(poses), 'input_pairs': len(stereo), 'full_pose_coverage': complete,
        'all_finite': finite, 'poses_sha256': sha256(out / 'poses.csv'),
        'backend_zero_observation_logs': obs.count(0),
        'backend_observation_logs': len(obs),
        'backend_valid_observations_median': float(np.median(obs)) if obs else None}
    if finite:
        result.update(max_position_norm_m=float(np.linalg.norm(values[:, :3], axis=1).max()),
                      estimated_path_m=float(np.linalg.norm(np.diff(values[:, :3], axis=0), axis=1).sum()))
    return result


def run_native(binary, variant, data, output, timeout):
    output.mkdir(exist_ok=True)
    calibration = data / ('calibration.yaml' if variant == 'baseline' else 'matched-imu.yaml')
    command = [str(binary), str(calibration), str(data / 'stereo.csv'), str(data / 'imu.csv'), str(output / 'poses.csv')]
    env = {k: v for k, v in os.environ.items() if not k.startswith('SCHUR_')}
    settings = {'SCHUR_MICRO_ROS': '0', 'SCHUR_PREDICTION': '0', 'SCHUR_TIMING_CSV': str(output / 'timing.csv'), **VARIANTS[variant]}
    env.update(settings)
    atomic_json(output / 'run.json', {'command': command, 'environment': settings, 'calibration_sha256': sha256(calibration)})
    started = time.monotonic()
    try:
        with (output / 'run.log').open('w') as log:
            process = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
        result = {'status': 'ok' if process.returncode == 0 else 'process_failed', 'returncode': process.returncode}
    except subprocess.TimeoutExpired:
        result = {'status': 'timeout'}
    result['wall_s'] = time.monotonic() - started
    stereo = rows(data / 'stereo.csv')
    if (output / 'poses.csv').exists():
        try:
            result.update(health(output, stereo))
            if result['status'] == 'ok' and not (result['full_pose_coverage'] and result['all_finite']):
                result['status'] = 'invalid_output'
            if result['status'] == 'ok' and (data / 'groundtruth.csv').exists():
                result.update(evaluate(output, stereo, data / 'groundtruth.csv'))
        except (ValueError, IndexError) as error:
            result.update(status='invalid_output', error=str(error))
    atomic_json(output / 'result.json', result)
    return result


def run_openvins(binary, data, output, template, chain, noise, timeout):
    output.mkdir(exist_ok=True)
    config = read_yaml(template)
    config.update(relative_config_imu='imu.yaml', relative_config_imucam='cameras.yaml')
    (output / 'config.yaml').write_text(opencv_yaml(config))
    cameras = copy.deepcopy(chain)
    for key, value in cameras.items():
        value['timeshift_cam_imu'] = 0.0  # stereo.csv already uses the corrected IMU clock
        value['cam_overlaps'] = [1] if key == 'cam0' else [0]
    (output / 'cameras.yaml').write_text(opencv_yaml(cameras))
    (output / 'imu.yaml').write_text(opencv_yaml({'imu0': noise}))
    command = [str(binary), str(output / 'config.yaml'), str(data), str(output / 'trajectory.csv'), '0']
    atomic_json(output / 'run.json', {'command': command, 'timestamp_policy': 'Input camera offset already applied; initial estimator offset zero; online time calibration follows the existing reference preset.'})
    started = time.monotonic()
    try:
        with (output / 'run.log').open('w') as log:
            process = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
        result = {'status': 'ok' if process.returncode == 0 else 'process_failed', 'returncode': process.returncode}
    except subprocess.TimeoutExpired:
        result = {'status': 'timeout'}
    result['wall_s'] = time.monotonic() - started
    if not (output / 'trajectory.csv').exists():
        result.update(status='process_failed', error='No trajectory output; see run.log')
        atomic_json(output / 'result.json', result)
        return result
    with (output / 'trajectory.csv').open() as stream:
        raw = list(csv.DictReader(stream))
    initialized = [row for row in raw if row['initialized'] == '1']
    write_csv(output / 'poses.csv', 'timestamp_ns,px,py,pz,qx,qy,qz,qw',
        [[row['timestamp_ns'], *[row[k] for k in ('px','py','pz','qx','qy','qz','qw')]] for row in initialized])
    expected = rows(data / 'stereo.csv')
    result.update(input_pairs=len(expected), processed_pairs=len(raw), initialized_pairs=len(initialized),
                  input_coverage=len(raw)/len(expected), initialized_fraction=len(initialized)/len(expected))
    # Initialization omissions are reported explicitly; ATE is always computed
    # on the common timestamps of all compared methods by the suite report.
    result['common_ate_pending'] = True
    atomic_json(output / 'result.json', result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, default=Path(__file__).with_name('uzh-sequences.json'))
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--noise-reference', type=Path, required=True)
    parser.add_argument('--openvins-binary', type=Path)
    parser.add_argument('--openvins-config', type=Path)
    parser.add_argument('--timeout', type=int, default=300)
    parser.add_argument('--ram-root', type=Path, default=Path('/dev/shm'))
    args = parser.parse_args()
    for key in ('cache', 'output', 'binary', 'noise_reference'):
        setattr(args, key, getattr(args, key).resolve())
    if args.openvins_binary:
        args.openvins_binary = args.openvins_binary.resolve()
        args.openvins_config = args.openvins_config.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads(args.manifest.read_text())
    noise = read_yaml(args.noise_reference)['imu0']
    state_file = args.output / 'suite.json'
    state = json.loads(state_file.read_text()) if state_file.exists() else {'source': manifest['source'], 'sequences': {}}
    current_binary = sha256(args.binary)
    current_library = sha256(args.binary.parent / 'libschurvins.so')
    changed_binary = state.get('binary_sha256', current_binary) != current_binary or state.get('library_sha256', current_library) != current_library
    for name,sequence in state['sequences'].items():
        for variant in list(sequence.get('runs', {})):
            if variant not in VARIANTS:
                continue
            metadata = args.output/name/variant/'run.json'
            calibration_file = args.output/name/'data'/('calibration.yaml' if variant == 'baseline' else 'matched-imu.yaml')
            recorded = json.loads(metadata.read_text()) if metadata.exists() else {}
            changed_config = not calibration_file.exists() or recorded.get('calibration_sha256') != sha256(calibration_file)
            if changed_binary or changed_config:
                del sequence['runs'][variant]
                sequence['status'] = 'cache_invalid'
                print('INVALIDATE', name, variant, 'binary/configuration fingerprint changed', flush=True)
    state['binary_sha256'] = sha256(args.binary)
    state['library_sha256'] = sha256(args.binary.parent / 'libschurvins.so')
    state['noise_reference_sha256'] = sha256(args.noise_reference)
    if args.openvins_binary:
        state['openvins_binary_sha256'] = sha256(args.openvins_binary)
    cv2.setNumThreads(1)
    pending = [item for item in manifest['sequences'] if state['sequences'].get(item['name'], {}).get('status') not in ('complete', 'unsupported', 'prepare_failed')]
    pending.sort(key=lambda x: (not x['public_gt'], x['name']))
    while pending:
        available = next((item for item in pending if (args.cache / (item['name'] + '.zip')).exists()), None)
        if available is None:
            time.sleep(5)
            continue
        item = available
        name = item['name']
        sequence = args.output / name
        data = sequence / 'data'
        current = state['sequences'].setdefault(name, {'public_gt': item['public_gt'], 'group': item['group'], 'runs': {}})
        try:
            print('PREPARE', name, flush=True)
            with tempfile.TemporaryDirectory(prefix='uzh-suite-', dir=args.ram_root) as temporary:
                metadata = prepare(args.cache, item, data, Path(temporary) / 'images', noise)
                current['input'] = metadata
                if metadata['status'] == 'unsupported':
                    current.update(status='unsupported', reason=metadata['reason'])
                else:
                    current['status'] = 'running'
                    atomic_json(state_file, state)
                    for variant in VARIANTS:
                        if variant not in current['runs']:
                            result = run_native(args.binary, variant, data, sequence / variant, args.timeout)
                            current['runs'][variant] = result
                            atomic_json(state_file, state)
                            print(name, variant, result['status'], 'ATE', result.get('ate_se3_rmse_m'), flush=True)
                    if args.openvins_binary and item['public_gt'] and 'openvins' not in current['runs']:
                        chain, _ = calibration(args.cache, item)
                        result = run_openvins(args.openvins_binary, data, sequence / 'openvins', args.openvins_config, chain, noise, args.timeout)
                        current['runs']['openvins'] = result
                    current['status'] = 'complete'
        except Exception as error:
            current.update(status='prepare_failed', error=repr(error))
            print('ERROR', name, repr(error), flush=True)
        atomic_json(state_file, state)
        pending.remove(item)
        print('DONE', name, 'remaining', len(pending), flush=True)
    print('SUITE COMPLETE', flush=True)


if __name__ == '__main__':
    main()
