#!/usr/bin/env python3
"""Converter regression tests; run with the rosbags/numpy/PyYAML environment."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import numpy as np
import yaml
from rosbags.highlevel import AnyReader
from rosbags.rosbag2 import Writer
from rosbags.rosbag1 import Writer as Writer1
from rosbags.typesys import Stores, get_typestore


class ConversionTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.store = get_typestore(Stores.ROS2_HUMBLE)
        self.topics = ['/left', '/right']
        camera = dict(camera_model='pinhole', distortion_model='radtan',
                      intrinsics=[100., 100., 1., 1.], distortion_coeffs=[0.] * 4,
                      resolution=[2, 2])
        self.cam = {'cam0': dict(camera, rostopic='/left'),
                    'cam1': dict(camera, rostopic='/right',
                                 T_cn_cnm1=np.diag([-1., -1., 1., 1.]).tolist())}
        # Synthetic positive values ONLY for exercising validation/conversion.
        self.imu = dict(rostopic='/imu', update_rate=200., accelerometer_noise_density=.01,
                        accelerometer_random_walk=.001, gyroscope_noise_density=.01,
                        gyroscope_random_walk=.001)

    def bag(self, with_imu=True, offset=0, duplicate=False):
        t = self.store.types
        def header(ns):
            return t['std_msgs/msg/Header'](t['builtin_interfaces/msg/Time'](*divmod(ns, 10**9)), 'sensor')
        with Writer(self.root / 'input') as w:
            images = [w.add_connection(topic, 'sensor_msgs/msg/Image', typestore=self.store)
                      for topic in self.topics]
            imuc = w.add_connection('/imu', 'sensor_msgs/msg/Imu', typestore=self.store) if with_imu else None
            for n in range(210):
                ns = 10**9 + n * 5_000_000
                if n % 10 == 0:
                    for side, c in enumerate(images):
                        if n == 200 and side == 1:  # One unpaired image.
                            continue
                        msg = t[c.msgtype](header(ns), 2, 2, 'mono8', 0, 2,
                                           np.array([n, side, 42, 255], dtype=np.uint8))
                        w.write(c, ns + 10**12, self.store.serialize_cdr(msg, c.msgtype))
                if imuc:
                    stamp = ns + offset - (5_000_000 if duplicate and n == 10 else 0)
                    msg = t[imuc.msgtype](header(stamp), t['geometry_msgs/msg/Quaternion'](0., 0., 0., 1.),
                         np.zeros(9), t['geometry_msgs/msg/Vector3'](.1, .2, .3), np.zeros(9),
                         t['geometry_msgs/msg/Vector3'](1., 2., 9.81), np.zeros(9))
                    w.write(imuc, ns + 10**12, self.store.serialize_cdr(msg, imuc.msgtype))

    def convert(self, stereo=False, source=None):
        (self.root / 'cam.yaml').write_text(yaml.safe_dump(self.cam))
        (self.root / 'imu.yaml').write_text(yaml.safe_dump(self.imu))
        options = ['--topics', *self.topics] if stereo else [
            '--cam', str(self.root / 'cam.yaml'), '--imu', str(self.root / 'imu.yaml')]
        return subprocess.run([sys.executable, str(Path(__file__).with_name('convert.py')),
             str(source or self.root / 'input'), str(self.root / 'output.bag'), *options], capture_output=True, text=True)

    def test_ros1_preserves_serialized_messages_and_acquisition_times(self):
        self.bag()
        expected = {}
        store1 = get_typestore(Stores.ROS1_NOETIC)
        source = self.root/'input.bag'
        with AnyReader([self.root/'input']) as reader, Writer1(source) as writer:
            connections = {c.topic: writer.add_connection(c.topic, c.msgtype, typestore=store1)
                           for c in reader.connections}
            for conn, receipt, raw in reader.messages():
                serialized = bytearray(self.store.cdr_to_ros1(raw, conn.msgtype))
                serialized[:4] = (123).to_bytes(4, 'little')
                msg = store1.deserialize_ros1(serialized, conn.msgtype)
                stamp = msg.header.stamp.sec*10**9 + msg.header.stamp.nanosec
                expected[(conn.topic, stamp)] = bytes(serialized)
                writer.write(connections[conn.topic], receipt, serialized)
        result = self.convert(source=source)
        self.assertEqual(result.returncode, 0, result.stderr)
        summary = json.loads(result.stdout)
        self.assertEqual(summary['source_format'], 'ros1')
        self.assertEqual(summary['stereo_pairs'], 20)
        self.assertEqual(summary['imu']['messages'], 210)
        count = 0
        with AnyReader([self.root/'output.bag']) as reader:
            for conn, stamp, raw in reader.messages():
                self.assertEqual(bytes(raw), expected[(conn.topic, stamp)])
                msg = reader.deserialize(raw, conn.msgtype)
                self.assertEqual(msg.header.seq, 123)
                self.assertEqual(stamp, msg.header.stamp.sec*10**9 + msg.header.stamp.nanosec)
                count += 1
        self.assertEqual(count, 250)

    def test_preserves_samples_and_acquisition_times(self):
        self.bag()
        result = self.convert()
        self.assertEqual(result.returncode, 0, result.stderr)
        summary = json.loads(result.stdout)
        self.assertEqual(summary['stereo_pairs'], 20)
        self.assertEqual(summary['imu']['messages'], 210)
        self.assertEqual(summary['imu']['mean_rate_hz'], 200.)
        counts = dict.fromkeys(['/left', '/right', '/imu'], 0)
        with AnyReader([self.root / 'output.bag']) as reader:
            for c, ns, raw in reader.messages():
                msg = reader.deserialize(raw, c.msgtype)
                self.assertEqual(ns, msg.header.stamp.sec * 10**9 + msg.header.stamp.nanosec)
                self.assertEqual(msg.header.frame_id, 'sensor')
                if c.topic == '/imu':
                    self.assertEqual(msg.linear_acceleration.z, 9.81)
                    self.assertEqual(msg.angular_velocity.y, .2)
                else:
                    self.assertEqual(msg.data.tolist(), [(ns-10**9)//5_000_000,
                                      int(c.topic == '/right'), 42, 255])
                counts[c.topic] += 1
        self.assertEqual(counts, {'/left': 20, '/right': 20, '/imu': 210})

    def test_stereo_compatibility(self):
        self.bag(with_imu=False)
        self.assertEqual(self.convert(stereo=True).returncode, 0)

    def test_missing_imu(self):
        self.bag(with_imu=False)
        self.assertIn('Missing requested topics', self.convert().stderr)

    def test_no_temporal_overlap(self):
        self.bag(offset=10**12)
        self.assertIn('overlapping', self.convert().stderr)

    def test_duplicate_imu_stamp(self):
        self.bag(duplicate=True)
        self.assertIn('Non-increasing', self.convert().stderr)

    def test_placeholder_noise(self):
        self.bag()
        self.imu['gyroscope_noise_density'] = None
        self.assertIn('not null', self.convert().stderr)

    def test_resolution_mismatch(self):
        self.bag()
        self.cam['cam0']['resolution'] = [640, 400]
        self.assertIn('resolution differs', self.convert().stderr)


if __name__ == '__main__':
    unittest.main()
