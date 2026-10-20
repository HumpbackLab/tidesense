#!/usr/bin/env python3
import io
import subprocess
import tempfile
import unittest
from pathlib import Path

import decode

HERE = Path(__file__).resolve().parent


class ProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temp.name)
        executable = cls.root / "protocol-test"
        subprocess.run(["cc", "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", "-pthread",
                        str(HERE / "test_protocol.c"), str(HERE / "protocol.c"), str(HERE / "imu.c"),
                        "-o", str(executable)], check=True)
        cls.stream = subprocess.check_output([str(executable)])
        cls.frames = list(decode.iter_jpegs(io.BytesIO(cls.stream)))
        cls.uvc_test = cls.root / "uvc-test"
        subprocess.run(["cc", "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", "-pthread",
                        str(HERE / "test_uvc.c"), str(HERE / "output.c"), str(HERE / "protocol.c"),
                        str(HERE / "imu.c"), "-o", str(cls.uvc_test),
                        *[f"-Wl,--wrap={name}" for name in ("open", "close", "ioctl", "poll", "mmap", "munmap")]],
                       check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_c_encoder_python_decoder_signed_axes_and_marker_bytes(self):
        self.assertEqual(len(self.frames), 3)
        meta = decode.metadata(self.frames[0])
        self.assertEqual(meta["frame_seq"], 1)
        self.assertEqual(meta["left_vi_ns"], 1025000000)
        self.assertEqual(meta["right_vi_ns"], 1027600000)
        self.assertEqual(meta["flags"], 17)
        self.assertEqual(meta["imu"][0]["raw"], [-32768, 32767, -9729, -1234, 42, 0])
        self.assertEqual(meta["imu"][0]["timestamp_ns"], 1002500000)
        self.assertAlmostEqual(meta["imu"][0]["gyro_rad_s"][0], -1234 * .001065264)

    def test_sdk_ioctl_success_and_empty_events(self):
        result = subprocess.run([str(self.uvc_test)], text=True, capture_output=True, check=True)
        self.assertIn("IMU committed exactly once", result.stdout)

    def test_stream_rates_and_exports(self):
        source = self.root / "capture.mjpg"
        source.write_bytes(self.stream)
        summary = decode.decode(source, self.root / "decoded", extract=True)
        self.assertEqual(summary["frames"], 3)
        self.assertEqual(summary["imu_samples"], 30)
        self.assertEqual(summary["image_hz"], 40)
        self.assertEqual(summary["imu_hz"], 400)
        self.assertEqual(summary["imu_gap_events"], 0)
        self.assertEqual(len((self.root / "decoded/imu.csv").read_text().splitlines()), 31)

    def test_truncation_and_malformed_lengths(self):
        for data in (self.stream[:-1], b"\xff\xd8\xff\xef\x00\x01", b"not a JPEG"):
            with self.assertRaises(ValueError):
                list(decode.iter_jpegs(io.BytesIO(data)))
        frame = bytearray(self.frames[0])
        frame[6 + 56:6 + 60] = (999).to_bytes(4, "little")
        with self.assertRaisesRegex(ValueError, "sample count"):
            decode.metadata(frame)

    def test_rejects_duplicate_frames(self):
        source = self.root / "duplicate.mjpg"
        source.write_bytes(self.frames[0] * 2)
        with self.assertRaisesRegex(ValueError, "backwards"):
            decode.decode(source)

    def test_entropy_escapes_and_restart_markers(self):
        frame = self.frames[0][:-2] + b"\xff\xda\x00\x02abc\xff\x00x\xff\xd0z\xff\xd9"
        self.assertEqual(list(decode.iter_jpegs(io.BytesIO(frame))), [frame])
        self.assertEqual(decode.metadata(frame)["frame_seq"], 1)


if __name__ == "__main__":
    unittest.main()
