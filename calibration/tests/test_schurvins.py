#!/usr/bin/env python3
"""Calibration conversion: frame direction, time units, noise units and safe CLI."""
import copy
from pathlib import Path
import tempfile
import unittest

import numpy as np
import yaml
from tide_calibration.schurvins import convert, write_config


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
    imu = dict(update_rate=199.52, model='scale-misalignment',
               accelerometers={'M': np.eye(3).tolist()},
               gyroscopes={'M': np.eye(3).tolist(), 'C_gyro_i': np.eye(3).tolist(),
                           'A': np.zeros((3, 3)).tolist()},
               gyroscope_noise_density=.001, accelerometer_noise_density=.002,
               gyroscope_random_walk=.00001, accelerometer_random_walk=.00002)
    return chain, {'imu0': imu}


class ExportTest(unittest.TestCase):
    def test_capture_resolutions(self):
        for size in ([640, 400], [640, 480], [1280, 800]):
            chain, imu = fixture()
            for camera in chain.values(): camera['resolution'] = size
            result, _ = convert(chain, imu)
            for camera in result['cameras']:
                self.assertEqual([camera['camera']['image_width'], camera['camera']['image_height']], size)
        chain['cam1']['resolution'] = [640, 400]
        with self.assertRaisesRegex(ValueError, 'matching'):
            convert(chain, imu)

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
        for key, value in [('model', 'scale-misalignment-size-effect'), ('model', 'calibrated'),
                           ('model', None), ('update_rate', 100),
                           ('gyroscope_random_walk', 0), ('accelerometer_noise_density', float('nan')),
                           ('time_offset', .1), ('T_i_b', np.diag([-1, -1, 1, 1]).tolist())]:
            bad = copy.deepcopy(imu)
            bad['imu0'][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                convert(chain, bad)

    def test_full_intrinsic_forward_inverse(self):
        chain, document = fixture()
        imu = document['imu0']
        ma = np.array([[1.02,0,0],[.01,.98,0],[-.02,.03,1.01]])
        mw = np.array([[.99,0,0],[.02,1.03,0],[.01,-.01,1.02]])
        angle = .12
        c = np.array([[np.cos(angle),-np.sin(angle),0],[np.sin(angle),np.cos(angle),0],[0,0,1.]])
        a = np.array([[.001,.002,0],[0,-.003,.001],[.002,0,.001]])
        imu.update(model='scale-misalignment', accelerometers={'M':ma.tolist()},
                   gyroscopes={'M':mw.tolist(),'C_gyro_i':c.tolist(),'A':a.tolist()})
        result, _ = convert(chain, document)
        p = result['imu_params']
        self.assertEqual(p['input_domain'], 'raw_si')
        rng = np.random.default_rng(42)
        acceleration, omega = rng.normal(size=(2,100,3))
        measured_a = acceleration @ ma.T
        measured_w = omega @ c.T @ mw.T + acceleration @ c.T @ a.T
        recovered_a = measured_a @ np.asarray(p['accel_inverse_matrix']).T
        recovered_w = measured_w @ np.asarray(p['gyro_inverse_matrix']).T + recovered_a @ np.asarray(p['gyro_accel_matrix']).T
        np.testing.assert_allclose(recovered_a, acceleration, atol=1e-12)
        np.testing.assert_allclose(recovered_w, omega, atol=1e-12)
        # A new session's bias remains in the corrected coordinates for VIO to estimate.
        ba, bg = np.array([.1,-.2,.3]), np.array([.01,.02,-.01])
        biased_a = (measured_a+ba) @ np.asarray(p['accel_inverse_matrix']).T
        biased_w = (measured_w+bg) @ np.asarray(p['gyro_inverse_matrix']).T + biased_a @ np.asarray(p['gyro_accel_matrix']).T
        expected_ba = np.linalg.solve(ma,ba)
        expected_bg = c.T @ np.linalg.solve(mw,bg) + np.asarray(p['gyro_accel_matrix']) @ expected_ba
        np.testing.assert_allclose(biased_a-acceleration, np.tile(expected_ba,(100,1)), atol=1e-12)
        np.testing.assert_allclose(biased_w-omega, np.tile(expected_bg,(100,1)), atol=1e-12)
        for key, bad in [('M',np.zeros((3,3)).tolist()),('M',[[float('nan')]*3]*3),('C_gyro_i',np.diag([1,1,-1]).tolist())]:
            invalid = copy.deepcopy(document)
            invalid['imu0']['gyroscopes'][key] = bad
            with self.assertRaises(ValueError):
                convert(chain,invalid)

    def test_equidistant_camera(self):
        chain, imu = fixture()
        for camera in chain.values():
            camera['distortion_model']='equidistant'
        result, _ = convert(chain,imu)
        self.assertEqual(result['cameras'][0]['camera']['distortion']['type'],'equidistant')

    def test_no_time_calibration_defaults_to_zero(self):
        chain, imu = fixture()
        for c in chain.values():
            del c['timeshift_cam_imu']
        result, _ = convert(chain, imu)
        self.assertEqual(result['live_camera_time_offset_ns'], 0)


    def test_config_exclusive_creation(self):
        chain, imu = fixture()
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'schurvins.yaml'
            write_config(chain, imu, path)
            original = path.read_bytes()
            with self.assertRaises(FileExistsError):
                write_config(chain, imu, path)
            self.assertEqual(path.read_bytes(), original)

if __name__ == '__main__':
    unittest.main()
