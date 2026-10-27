#!/usr/bin/env python3
"""Install/check the fixed Kalibr IMU policy in the container at build time."""
import argparse
import hashlib
import json
from pathlib import Path

REVISION = '1f60227442d25e36365ef5f72cd80b9666d73467'
MODULE = Path('/catkin_ws/src/kalibr/aslam_offline_calibration/kalibr/python/kalibr_imu_camera_calibration/IccSensors.py')
ORIGINAL_SHA256 = '1594682a604691bb2b0ef2fc3f1ee0afe149ac23a8c68158f7788af21a52d473'
MARKER = Path('/usr/local/share/tidesense-calibration.json')
NEEDLE = b'self.M_accel_gyro_Dv.setActive(True)'
REPLACEMENT = b'self.M_accel_gyro_Dv.setActive(False)'


def configure():
    original = MODULE.read_bytes()
    if hashlib.sha256(original).hexdigest() != ORIGINAL_SHA256 or original.count(NEEDLE) != 1:
        raise RuntimeError('Expected the pinned, unmodified Kalibr IccSensors.py')
    patched = original.replace(NEEDLE, REPLACEMENT)
    MODULE.write_bytes(patched)
    MARKER.write_text(json.dumps(dict(kalibr_revision=REVISION, imu_model='scale-misalignment',
        gyro_g_sensitivity_estimated=False, original_sha256=ORIGINAL_SHA256,
        patched_sha256=hashlib.sha256(patched).hexdigest()), indent=2) + '\n')


def check():
    # Import the solver dependencies before accepting any recording for processing.
    import numpy
    import scipy.optimize
    import yaml
    # Match Kalibr's entrypoint import order: kalibr_common loads the sparse backend.
    import sm
    import kalibr_common
    import kalibr_imu_camera_calibration
    marker = json.loads(MARKER.read_text())
    if (marker['kalibr_revision'] != REVISION or marker['imu_model'] != 'scale-misalignment'
            or marker['gyro_g_sensitivity_estimated'] is not False
            or marker['original_sha256'] != ORIGINAL_SHA256
            or marker['patched_sha256'] != hashlib.sha256(MODULE.read_bytes()).hexdigest()
            or MODULE.read_bytes().count(REPLACEMENT) != 1):
        raise RuntimeError('Kalibr environment does not match the fixed TideSense IMU policy')
    print('Kalibr environment OK: scale-misalignment, gyro acceleration sensitivity fixed zero')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    check() if args.check else configure()
