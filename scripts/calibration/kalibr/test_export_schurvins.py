#!/usr/bin/env python3
"""Calibration conversion: frame direction, time units, noise units and safe CLI."""
import copy
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import numpy as np
import yaml
from export_schurvins import convert


def fixture():
    chain = {}
    for i in range(2):
        t = np.eye(4)
        if i == 0:
            t[:3, :3] = np.diag([-1., -1., 1.])
        t[:3, 3] = [.01 - i*.05, .02, -.03]
        chain[f'cam{i}'] = dict(camera_model='pinhole', distortion_model='radtan',
                               resolution=[640, 400], intrinsics=[400., 401., 320., 200.],
                               distortion_coeffs=[-.3, .08, .001, -.002],
                               T_cam_imu=t.tolist(), timeshift_cam_imu=-.0104616674)
    chain['cam1']['T_cn_cnm1'] = (np.array(chain['cam1']['T_cam_imu']) @
                                 np.linalg.inv(chain['cam0']['T_cam_imu'])).tolist()
    imu = dict(update_rate=199.52, model='calibrated',
               gyroscope_noise_density=.001, accelerometer_noise_density=.002,
               gyroscope_random_walk=.00001, accelerometer_random_walk=.00002)
    return chain, {'imu0': imu}


class ExportTest(unittest.TestCase):
    def test_frames_intrinsics_and_rotated_camera(self):
        chain, imu = fixture()
        result, _ = convert(chain, imu)
        for i, camera in enumerate(result['cameras']):
            inverse = np.array(camera['T_B_C']['data']).reshape(4, 4)
            np.testing.assert_allclose(inverse @ chain[f'cam{i}']['T_cam_imu'], np.eye(4), atol=1e-12)
            self.assertEqual(camera['camera']['intrinsics']['data'], [400., 401., 320., 200.])
            self.assertEqual(camera['camera']['distortion']['parameters']['data'], [-.3, .08, .001, -.002])
        self.assertEqual(result['cameras'][0]['T_B_C']['data'][0], -1.)

    def test_time_sign_noise_units_and_residual_bias(self):
        chain, imu = fixture()
        chain['cam1']['timeshift_cam_imu'] = .02
        result, _ = convert(chain, imu)
        self.assertEqual(result['live_camera_time_offset_ns'], -10461667)
        p = result['imu_params']
        self.assertEqual(p['delay_imu_cam'], 0)
        self.assertEqual(p['imu_rate'], 200)
        self.assertEqual([p[k] for k in ['sigma_omega_c', 'sigma_acc_c', 'sigma_omega_bias_c', 'sigma_acc_bias_c']],
                         [.001, .002, .00001, .00002])
        self.assertEqual(result['imu_initialization']['acc_bias'], [0., 0., 0.])

    def test_bad_camera_metadata_and_geometry_rejected(self):
        chain, imu = fixture()
        mutations = [('resolution', [320, 240]), ('camera_model', 'omni'),
                     ('intrinsics', [float('nan'), 1, 1, 1]),
                     ('T_cam_imu', np.zeros((4, 4)).tolist()),
                     ('timeshift_cam_imu', float('nan'))]
        for key, value in mutations:
            bad = copy.deepcopy(chain)
            bad['cam0'][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                convert(bad, imu)
        chain['cam1']['T_cn_cnm1'][0][3] += .01
        with self.assertRaises(ValueError):
            convert(chain, imu)

    def test_bad_imu_rejected(self):
        chain, imu = fixture()
        for key, value in [('model', 'scale-misalignment'), ('update_rate', 100),
                           ('gyroscope_random_walk', 0), ('accelerometer_noise_density', float('nan')),
                           ('time_offset', .1), ('T_i_b', np.diag([-1, -1, 1, 1]).tolist())]:
            bad = copy.deepcopy(imu)
            bad['imu0'][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                convert(chain, bad)

    def test_no_time_calibration_defaults_to_zero(self):
        chain, imu = fixture()
        for c in chain.values():
            del c['timeshift_cam_imu']
        result, _ = convert(chain, imu)
        self.assertEqual(result['live_camera_time_offset_ns'], 0)

    def test_imu_cam_automatically_exports_but_convert_only_does_not(self):
        # Fake only the expensive Podman/Kalibr boundary; execute the real shell
        # orchestration and exporter, including paths containing spaces.
        script = Path(__file__).resolve().parents[1] / 'kalibr.sh'
        chain, imu = fixture()
        with tempfile.TemporaryDirectory(prefix='schur pipeline ') as tmp:
            root = Path(tmp)
            bag = root / 'bag'
            bag.mkdir()
            (bag / 'metadata.yaml').write_text('fixture')
            (root / 'camera.yaml').write_text(yaml.safe_dump(chain))
            (root / 'imu.yaml').write_text(yaml.safe_dump(imu))
            fake = root / 'podman'
            fake.write_text("""#!/usr/bin/env python3
import os, pathlib, shutil, subprocess, sys
args = sys.argv[1:]
if args[0] == 'image':
    print('{}')
    sys.exit(0)
mounts = [arg for arg in args if arg.endswith(':/output:rw')]
if not mounts:
    sys.exit(0)
out = pathlib.Path(mounts[0][:-len(':/output:rw')])
if 'kalibr_calibrate_imu_camera' in args:
    source = pathlib.Path(os.environ['EXPORT_FIXTURES'])
    shutil.copyfile(source/'camera.yaml', out/'imucam-camchain-imucam.yaml')
    shutil.copyfile(source/'imu.yaml', out/'imucam-imu.yaml')
    for name in ('imucam-results-imucam.txt', 'imucam-report-imucam.pdf'):
        (out/name).write_text('fixture')
elif '/output/export_schurvins.py' in args:
    sys.exit(subprocess.call([sys.executable, str(out/'export_schurvins.py'), str(out)]))
""")
            fake.chmod(0o755)
            env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ['PATH'],
                       EXPORT_FIXTURES=str(root))
            for only in (False, True):
                output = root / ('convert only' if only else 'full')
                command = ['bash', str(script), str(bag), '--imu-cam',
                           '--cam', str(root/'camera.yaml'), '--imu', str(root/'imu.yaml'),
                           '--output', str(output)]
                if only:
                    command.append('--convert-only')
                subprocess.run(command, env=env, check=True, capture_output=True)
                self.assertEqual((output/'schurvins.yaml').exists(), not only)
                if not only:
                    self.assertIn('SchurVINS configuration', (output/'schurvins-export.log').read_text())

    def test_one_click_existing_results_and_no_overwrite(self):
        chain, imu = fixture()
        script = Path(__file__).resolve().parents[1] / 'kalibr.sh'
        with tempfile.TemporaryDirectory(prefix='schur export ') as tmp:
            folder = Path(tmp)
            (folder / 'imucam-camchain-imucam.yaml').write_text(yaml.safe_dump(chain))
            (folder / 'imucam-imu.yaml').write_text(yaml.safe_dump(imu))
            command = ['bash', str(script), '--export-schurvins', str(folder)]
            subprocess.run(command, check=True, capture_output=True)
            output = folder / 'schurvins.yaml'
            original = output.read_bytes()
            self.assertEqual(yaml.safe_load(original)['imu_params']['imu_rate'], 200)
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(original, output.read_bytes())
            subprocess.run(command + ['--output', str(folder / 'alternate.yaml')], check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
