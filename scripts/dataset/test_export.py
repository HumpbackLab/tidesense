#!/usr/bin/env python3
"""Format/corruption tests, including actual ROS1 serialization and readback."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import yaml
import zlib

spec = importlib.util.spec_from_file_location('dataset_export', Path(__file__).with_name('export.py'))
e = importlib.util.module_from_spec(spec)
spec.loader.exec_module(e)

class ExportTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.src = self.root/'capture'
        self.src.mkdir()
        (self.src/'COMPLETE').touch()
        (self.src/'metadata.yaml').write_text(yaml.safe_dump(dict(schema='tidesense-dataset-v1', complete=True, pairs=3, imu_samples=41)))
        with (self.src/'frames.bin').open('wb') as f:
            f.write(e.MAGIC)
            for i in range(3):
                t = 1000000000+i*50000000
                f.write(e.HEADER.pack(0x54444341, 1, 640, 400, i+1, t, t+2000000, i+10, i+10, 1, 0))
                f.write(bytes([i])*e.PIXELS + bytes([255-i])*e.PIXELS)
        (self.src/'imu.csv').write_text('#timestamp_ns,gx,gy,gz,ax,ay,az,raw_ax,raw_ay,raw_az\n'+''.join(
            f'{950000000+i*5000000},0.1,0.2,0.3,1,2,9.8,1.1,2.2,9.1\n' for i in range(41)))
        (self.src/'calibration.yaml').write_text(yaml.safe_dump(dict(cameras=[{}, {}], live_camera_time_offset_ns=10000000)))

    def test_roundtrip_bag_and_png(self):
        out = self.root/'export'
        report = e.export(self.src, out, write_bag=True)
        self.assertEqual(report['camera_offset_ns'], 10000000)
        self.assertTrue((out/'COMPLETE').exists())
        self.assertFalse((out/'INCOMPLETE').exists())
        self.assertEqual(yaml.safe_load((out/'replay.yaml').read_text())['live_camera_time_offset_ns'], 0)
        image = (out/'mav0/cam1/data/1010000000.png').read_bytes()
        n = struct.unpack_from('>I', image, 33)[0]
        self.assertEqual(zlib.decompress(image[41:41+n]), (b'\0'+b'\xff'*640)*400)
        from rosbags.highlevel import AnyReader
        with AnyReader([out/'dataset.bag']) as reader:
            counts = {}; last = 0
            for connection, stamp, raw in reader.messages():
                self.assertGreaterEqual(stamp, last); last = stamp
                msg = reader.deserialize(raw, connection.msgtype)
                self.assertEqual(stamp, msg.header.stamp.sec*10**9 + msg.header.stamp.nanosec)
                counts[connection.topic] = counts.get(connection.topic, 0)+1
                if connection.topic == '/imu0':
                    self.assertEqual(msg.orientation_covariance[0], -1)
                    self.assertEqual(msg.linear_acceleration.z, 9.8)
                else:
                    self.assertEqual(msg.encoding, 'mono8')
                    self.assertEqual(len(msg.data), e.PIXELS)
            self.assertEqual(counts, {'/cam0/image_raw': 3, '/cam1/image_raw': 3, '/imu0': 41})

    def compress_capture(self):
        import numpy as np
        import zstandard
        p = self.src/'frames.bin'
        data = p.read_bytes()[len(e.MAGIC):]
        compressed = bytearray(b'TIDESENSE_ZST_V1\n')
        for start in range(0, len(data), e.RECORD):
            frame = data[start:start+e.RECORD]
            pixels = np.frombuffer(frame[e.HEADER.size:], dtype=np.uint8).reshape(800, 640)
            filtered = pixels.copy()
            filtered[:, 1:] = pixels[:, 1:]-pixels[:, :-1]
            block = zstandard.ZstdCompressor(level=1, write_checksum=True).compress(frame[:e.HEADER.size]+filtered.tobytes())
            compressed.extend(struct.pack('<I', len(block))+block)
        p.write_bytes(compressed)

    def test_compressed_roundtrip(self):
        original = list(e.frames(self.src))
        self.compress_capture()
        self.assertEqual(list(e.frames(self.src)), original)
        self.test_roundtrip_bag_and_png()

    def test_checksum_corruption(self):
        self.compress_capture()
        p = self.src/'frames.bin'
        data = bytearray(p.read_bytes()); data[-1] ^= 1; p.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'checksum'):
            e.validate(self.src, 0)

    def test_reject_truncation(self):
        p = self.src/'frames.bin'
        with p.open('r+b') as f:
            f.truncate(p.stat().st_size-1)
        with self.assertRaisesRegex(ValueError, 'Truncated'):
            e.export(self.src, self.root/'bad')
        self.assertFalse((self.root/'bad').exists())

    def test_reject_imu_gap(self):
        p = self.src/'imu.csv'; lines = p.read_text().splitlines(True); del lines[5]
        p.write_text(''.join(lines))
        with self.assertRaisesRegex(ValueError, 'IMU gap'):
            e.validate(self.src, 0)

    def test_reject_incomplete(self):
        (self.src/'INCOMPLETE').touch()
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            e.validate(self.src, 0)

    def test_no_calibration_and_raw_time(self):
        (self.src/'calibration.yaml').unlink()
        out = self.root/'raw'
        report = e.export(self.src, out)
        self.assertEqual(report['camera_offset_ns'], 0)
        self.assertFalse(report['schur_replay_ready'])
        self.assertFalse((out/'replay.yaml').exists())

    def test_offset_requires_imu_coverage(self):
        with self.assertRaisesRegex(ValueError, 'bracket'):
            e.export(self.src, self.root/'bad', offset_override=100000000)

if __name__ == '__main__':
    unittest.main()
