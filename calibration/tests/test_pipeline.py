#!/usr/bin/env python3
"""Exercise the full host pipeline, replacing only the expensive Podman solver."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import shutil
import tempfile
import unittest

import numpy as np
import yaml
import zstandard
from rosbags.highlevel import AnyReader

from tide_calibration import recording as e

ROOT = Path(__file__).resolve().parents[1]


FAKE_PODMAN = r'''#!/usr/bin/env python3
import json, os, pathlib, shutil, subprocess, sys
import numpy as np
import yaml
args = sys.argv[1:]
with open(os.environ['BOUNDARY_EVENTS'], 'a') as stream:
    stream.write(json.dumps(args) + '\n')
if args[0] == 'image':
    print(json.dumps([{'Id': 'sha256:fixture'}]))
    sys.exit(0)
if '--check' in args:
    sys.exit(19 if os.environ.get('FAIL_ENV') else 0)
mounts = [v for v in args if v.endswith(':/output:rw')]
if not mounts:
    sys.exit(0)
out = pathlib.Path(mounts[0][:-len(':/output:rw')])
if 'kalibr_calibrate_cameras' in args:
    if os.environ.get('FAIL_STAGE') == 'cameras':
        print('intentional camera solver failure', flush=True)
        sys.exit(17)
    model = args[args.index('--models')+1:args.index('--models')+3]
    assert len(model) == 2 and model[0] == model[1]
    distortion = 'radtan' if model[0] == 'pinhole-radtan' else 'equidistant'
    relative = np.eye(4)
    relative[0, 3] = -.05
    chain = {f'cam{i}': dict(camera_model='pinhole', distortion_model=distortion,
             resolution=[640, 400], intrinsics=[400., 401., 320., 200.],
             distortion_coeffs=[-.03, .008, .001, -.002], rostopic=f'/cam{i}/image_raw')
             for i in range(2)}
    chain['cam1']['T_cn_cnm1'] = relative.tolist()
    (out/'dataset-camchain.yaml').write_text(yaml.safe_dump(chain))
    for name in ('dataset-results-cam.txt', 'dataset-report-cam.pdf'):
        (out/name).write_text('synthetic solver output')
elif 'kalibr_calibrate_imu_camera' in args:
    assert args[args.index('--imu-models')+1] == 'scale-misalignment'
    if os.environ.get('FAIL_STAGE') == 'imucam':
        print('intentional IMU solver failure', flush=True)
        sys.exit(18)
    chain = yaml.safe_load((out/'dataset-camchain.yaml').read_text())
    for i in range(2):
        transform = np.eye(4)
        transform[0, 3] = .01 - i*.05
        chain[f'cam{i}'].update(T_cam_imu=transform.tolist(), timeshift_cam_imu=.012+i*.00001)
    if os.environ.get('WRONG_LENS'):
        chain['cam1']['distortion_model'] = 'equidistant'
    (out/'dataset-camchain-imucam.yaml').write_text(yaml.safe_dump(chain))
    imu = yaml.safe_load((out/'imu.yaml').read_text())
    imu.update(model='scale-misalignment', accelerometers={'M': np.eye(3).tolist()},
               gyroscopes={'M': np.eye(3).tolist(), 'C_gyro_i': np.eye(3).tolist(), 'A': np.zeros((3,3)).tolist()})
    (out/'dataset-imu.yaml').write_text(yaml.safe_dump({'imu0': imu}))
    (out/'dataset-results-imucam.txt').write_text(
        'Normalized Residuals\nReprojection error (cam0): mean 100, median 100, std: 100\n'
        'Residuals\nReprojection error (cam0) [px]: mean 0.4, median 0.3, std: 0.2\n'
        'Reprojection error (cam1) [px]: mean 0.5, median 0.4, std: 0.3\n'
        'Gyroscope error (imu0) [rad/s]: mean 0.001, median 0.0009, std: 0.0002\n'
        'Accelerometer error (imu0) [m/s^2]: mean 0.02, median 0.01, std: 0.01\n')
    if not os.environ.get('OMIT_REPORT'):
        (out/'dataset-report-imucam.pdf').write_text('synthetic report')
'''


class PipelineTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='tidesense calibration ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'raw capture'
        self.source.mkdir()
        self.output = self.root / 'result'
        (self.source / 'COMPLETE').touch()
        (self.source / 'metadata.yaml').write_text(yaml.safe_dump(dict(schema='tidesense-dataset-v2', complete=True, pairs=21, imu_samples=231)))
        self.pixels = np.arange(2 * 400 * 640, dtype=np.uint8).reshape(800, 640)
        delta = self.pixels.copy()
        delta[:, 1:] = self.pixels[:, 1:] - self.pixels[:, :-1]
        with (self.source / 'frames.bin').open('wb') as stream:
            stream.write(b'TIDESENSE_ZST_V1\n')
            for i in range(21):
                t = 10**9 + i * 50000000
                header = e.HEADER.pack(0x54444341, 1, 640, 400, i+1, t, t+2000000, i+10, i+10, 1, 0)
                data = zstandard.ZstdCompressor(write_checksum=True).compress(header + delta.tobytes())
                stream.write(len(data).to_bytes(4, 'little') + data)
        (self.source / 'imu.csv').write_text(''.join(
            f'{950000000+i*5000000},0.1,0.2,0.3,1,2,9.8\n' for i in range(231)))
        # A deliberately incompatible old snapshot must have no influence on recalibration.
        (self.source / 'calibration.yaml').write_text('cameras: old\nlive_camera_time_offset_ns: 12345678\n')
        self.imu = self.root / 'measured noise.yaml'
        self.imu.write_text(yaml.safe_dump(dict(rostopic='/old/topic', update_rate=199.52,
            accelerometer_noise_density=.002, accelerometer_random_walk=.00002,
            gyroscope_noise_density=.001, gyroscope_random_walk=.00001)))
        binary = self.root / 'podman'
        binary.write_text(FAKE_PODMAN)
        binary.chmod(0o755)
        self.env = dict(os.environ, PATH=os.pathsep.join((str(self.root), str(Path(sys.executable).parent), os.environ['PATH'])),
                        BOUNDARY_EVENTS=str(self.root / 'events.jsonl'))

    def run_pipeline(self, lens='wide', **extra_env):
        return subprocess.run([sys.executable, str(getattr(self, 'script', ROOT / 'calibrate.py')), str(self.source),
                               '--imu', str(self.imu), '--lens', lens, '--output', str(self.output)],
                              env=dict(self.env, **extra_env), cwd=self.root, capture_output=True, text=True)

    def test_both_lenses_preserve_pixels_imu_and_raw_time(self):
        original = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in self.source.iterdir()}
        for lens, distortion in (('wide', 'radial-tangential'), ('fisheye', 'equidistant')):
            with self.subTest(lens=lens):
                self.output = self.root / lens
                process = self.run_pipeline(lens)
                self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
                summary = json.loads((self.output / 'calibration.json').read_text())
                self.assertEqual(summary['status'], 'complete')
                self.assertEqual(summary['quality'], 'requires_review')
                self.assertEqual(summary['imu_model'], 'scale-misalignment')
                self.assertAlmostEqual(summary['baseline_m'], .05)
                self.assertEqual(summary['applied_camera_offset_ns'], 12000000)
                self.assertEqual(summary['residuals']['Reprojection error (cam0)']['mean'], .4)
                self.assertEqual(len(summary['residuals']), 4)
                self.assertTrue((self.output / 'COMPLETE').exists())
                self.assertFalse((self.output / 'INCOMPLETE').exists())
                self.assertFalse((self.output / 'export/mav0').exists())
                self.assertIn('标定质量待复核', (self.output / 'report.md').read_text())
                config = yaml.safe_load((self.output / 'schurvins.yaml').read_text())
                self.assertTrue(all(cam['camera']['distortion']['type'] == distortion for cam in config['cameras']))
                self.assertEqual(list(self.output.rglob('*.bag')), [self.output / 'dataset.bag'])
                for stage in ('cameras', 'imucam'):
                    command = json.loads((self.output / f'{stage}-command.json').read_text())
                    self.assertIn(f'{self.output}/dataset.bag:/output/dataset.bag:ro', command)
                    self.assertEqual(command[command.index('--bag') + 1], '/output/dataset.bag')
                    self.assertIn('sha256:fixture', command)
                self.assertEqual(summary['image_id'], 'sha256:fixture')
                with AnyReader([self.output / 'dataset.bag']) as reader:
                    counts = {}; first_image = True
                    for conn, t, raw in reader.messages():
                        counts[conn.topic] = counts.get(conn.topic, 0) + 1
                        message = reader.deserialize(raw, conn.msgtype)
                        self.assertEqual(t, message.header.stamp.sec*10**9 + message.header.stamp.nanosec)
                        if conn.topic == '/imu0':
                            self.assertEqual(message.angular_velocity.x, .1)
                            self.assertEqual(message.linear_acceleration.z, 9.8)
                        else:
                            index = 0 if conn.topic == '/cam0/image_raw' else 1
                            self.assertEqual(bytes(message.data), self.pixels[index*400:(index+1)*400].tobytes())
                            if first_image:
                                self.assertEqual(t, 10**9)
                                first_image = False
                    self.assertEqual(counts, {'/cam0/image_raw': 21, '/cam1/image_raw': 21, '/imu0': 231})
                self.assertEqual(original, {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in self.source.iterdir()})

    def test_solver_failures_do_not_continue_or_report_success(self):
        for stage in ('cameras', 'imucam'):
            self.output = self.root / stage
            process = self.run_pipeline(FAIL_STAGE=stage)
            self.assertNotEqual(process.returncode, 0)
            self.assertTrue((self.output / 'INCOMPLETE').exists())
            self.assertFalse((self.output / 'COMPLETE').exists())
            self.assertFalse((self.output / 'schurvins.yaml').exists())
            manifest = json.loads((self.output / 'calibration.json').read_text())
            self.assertEqual((manifest['status'], manifest['stage']), ('failed', stage))
            log = self.output / (stage + '.log')
            self.assertIn('intentional', log.read_text())
            if stage == 'cameras':
                self.assertFalse((self.output / 'imucam').exists())

    def test_existing_output_refused_without_changes(self):
        self.output.mkdir()
        (self.output / 'keep').write_text('unchanged')
        process = self.run_pipeline()
        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(list(self.output.iterdir()), [self.output / 'keep'])
        self.assertEqual((self.output / 'keep').read_text(), 'unchanged')
        self.assertFalse((self.root / 'events.jsonl').exists())

    def test_output_inside_capture_refused(self):
        self.output = self.source / 'result'
        self.assertNotEqual(self.run_pipeline().returncode, 0)
        self.assertFalse(self.output.exists())

    def test_missing_noise_and_incomplete_recording_refused(self):
        self.imu.write_text(self.imu.read_text().replace('0.002', 'null'))
        self.assertNotEqual(self.run_pipeline().returncode, 0)
        self.assertFalse(self.output.exists())
        self.assertFalse((self.root / 'events.jsonl').exists())
        (self.source / 'INCOMPLETE').touch()
        self.imu.write_text(self.imu.read_text().replace('null', '0.002'))
        self.assertNotEqual(self.run_pipeline().returncode, 0)
        self.assertFalse(self.output.exists())

    def test_lens_mismatch_in_results_refused(self):
        process = self.run_pipeline(WRONG_LENS='1')
        self.assertNotEqual(process.returncode, 0)
        self.assertFalse((self.output / 'COMPLETE').exists())
        self.assertFalse((self.output / 'schurvins.yaml').exists())
        self.assertEqual(json.loads((self.output / 'calibration.json').read_text())['stage'], 'summary')

    def test_portable_without_legacy_directory(self):
        isolated = self.root / 'isolated' / 'calibration'
        shutil.copytree(ROOT, isolated, ignore=shutil.ignore_patterns('__pycache__'))
        self.script = isolated / 'calibrate.py'
        self.env.pop('PYTHONPATH', None)
        self.assertFalse((isolated.parent / 'scripts').exists())
        process = self.run_pipeline()
        self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
        self.assertTrue((self.output / 'schurvins.yaml').exists())

    def test_incompatible_environment_refused_before_export(self):
        process = self.run_pipeline(FAIL_ENV='1')
        self.assertNotEqual(process.returncode, 0)
        self.assertFalse(self.output.exists())

    def test_zero_exit_without_report_is_failure(self):
        process = self.run_pipeline(OMIT_REPORT='1')
        self.assertNotEqual(process.returncode, 0)
        self.assertFalse((self.output / 'COMPLETE').exists())
        self.assertFalse((self.output / 'schurvins.yaml').exists())
        state = json.loads((self.output / 'calibration.json').read_text())
        self.assertEqual((state['status'], state['stage']), ('failed', 'imucam'))
        self.assertIn('did not produce', state['error'])

    def test_removed_options_refused(self):
        for option in (['--imu-models', 'calibrated'], ['--resume']):
            process = subprocess.run([sys.executable, str(ROOT / 'calibrate.py'), str(self.source),
                '--imu', str(self.imu), *option], env=self.env, capture_output=True)
            self.assertNotEqual(process.returncode, 0)
            self.assertFalse((self.root / 'events.jsonl').exists())


if __name__ == '__main__':
    unittest.main()
