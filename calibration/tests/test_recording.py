"""Capture corruption checks and actual ROS1 pixel/IMU/time round trips."""
from pathlib import Path
import tempfile
import unittest

import numpy as np
from rosbags.highlevel import AnyReader
import yaml
import zstandard

from tide_calibration import recording as r


class RecordingTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'capture'
        self.source.mkdir()
        (self.source / 'COMPLETE').touch()
        self.metadata = dict(schema='tidesense-dataset-v2', complete=True, pairs=3, imu_samples=41)
        self.make_capture(640, 400)
        (self.source / 'imu.csv').write_text(''.join(
            f'{950000000 + i*5000000},0.1,0.2,0.3,1,2,9.8\n' for i in range(41)))

    def make_capture(self, width, height, compressed=True):
        self.metadata.update(width=width, height=height)
        (self.source / 'metadata.yaml').write_text(yaml.safe_dump(self.metadata))
        pixels = np.arange(2 * width * height, dtype=np.uint8).reshape(2 * height, width)
        residual = pixels.copy()
        residual[:, 1:] = pixels[:, 1:] - pixels[:, :-1]
        with (self.source / 'frames.bin').open('wb') as stream:
            stream.write(b'TIDESENSE_ZST_V1\n' if compressed else r.MAGIC)
            for i in range(3):
                t = 10**9 + i * 50000000
                data = r.HEADER.pack(0x54444341, 1, width, height, i+1, t, t+2000000, i+10, i+10, 1, 0)
                data += residual.tobytes() if compressed else pixels.tobytes()
                if compressed:
                    data = zstandard.ZstdCompressor(write_checksum=True).compress(data)
                    stream.write(len(data).to_bytes(4, 'little'))
                stream.write(data)
        return pixels

    def test_all_resolutions_raw_and_compressed_bag(self):
        for width, height in sorted(r.RESOLUTIONS):
            for compressed in (False, True):
                with self.subTest(size=(width, height), compressed=compressed):
                    pixels = self.make_capture(width, height, compressed)
                    report = r.validate(self.source)
                    self.assertEqual(report['resolution'], [width, height])
                    self.assertEqual(report['camera_offset_ns'], 0)
                    self.assertEqual((report['camera_hz'], report['imu_hz']), (20, 200))
                    destination = self.root / f'{width}-{height}-{compressed}.bag'
                    r.write_bag(self.source, destination)
                    counts = {}
                    with AnyReader([destination]) as reader:
                        for conn, stamp, raw in reader.messages():
                            message = reader.deserialize(raw, conn.msgtype)
                            counts[conn.topic] = counts.get(conn.topic, 0) + 1
                            self.assertEqual(stamp, message.header.stamp.sec*10**9 + message.header.stamp.nanosec)
                            if conn.topic == '/imu0':
                                self.assertEqual(message.linear_acceleration.z, 9.8)
                                self.assertEqual(message.angular_velocity.x, .1)
                            else:
                                index = 0 if conn.topic == '/cam0/image_raw' else 1
                                self.assertEqual((message.width, message.height, message.step), (width, height, width))
                                self.assertEqual(bytes(message.data), pixels[index*height:(index+1)*height].tobytes())
                    self.assertEqual(counts, {'/cam0/image_raw': 3, '/cam1/image_raw': 3, '/imu0': 41})

    def test_checksum_and_truncation_rejected(self):
        path = self.source / 'frames.bin'
        original = path.read_bytes()
        corrupt = bytearray(original)
        corrupt[-1] ^= 1
        path.write_bytes(corrupt)
        with self.assertRaisesRegex(ValueError, 'checksum'):
            r.validate(self.source)
        path.write_bytes(original[:-1])
        with self.assertRaisesRegex(ValueError, 'Truncated'):
            r.validate(self.source)

    def test_incomplete_rejected(self):
        (self.source / 'INCOMPLETE').touch()
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            r.validate(self.source)

    def test_bad_metadata_and_counts_rejected(self):
        path = self.source / 'metadata.yaml'
        path.write_text('[]')
        with self.assertRaisesRegex(ValueError, 'metadata'):
            r.validate(self.source)
        self.metadata['pairs'] = 4
        path.write_text(yaml.safe_dump(self.metadata))
        with self.assertRaisesRegex(ValueError, 'counts'):
            r.validate(self.source)

    def test_camera_gap_rejected(self):
        self.make_capture(640, 400, False)
        path = self.source / 'frames.bin'
        data = bytearray(path.read_bytes())
        header = list(r.HEADER.unpack_from(data, len(r.MAGIC)))
        header[7] = 99
        data[len(r.MAGIC):len(r.MAGIC)+r.HEADER.size] = r.HEADER.pack(*header)
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'Camera gap'):
            r.validate(self.source)

    def test_imu_gap_and_missing_coverage_rejected(self):
        path = self.source / 'imu.csv'
        lines = path.read_text().splitlines(True)
        path.write_text(''.join(lines[:4] + lines[5:]))
        with self.assertRaisesRegex(ValueError, 'IMU gap'):
            r.validate(self.source)
        path.write_text(''.join(lines[11:]))
        with self.assertRaisesRegex(ValueError, 'bracket'):
            r.validate(self.source)

    def test_legacy_imu_uses_original_si_columns(self):
        path = self.source / 'imu.csv'
        path.write_text(path.read_text().replace(',1,2,9.8\n', ',9,8,7,1,2,9.8\n'))
        rows = list(r.imu_rows(self.source))
        self.assertEqual(rows[0][1], [.1, .2, .3, 1, 2, 9.8])


if __name__ == '__main__':
    unittest.main()
